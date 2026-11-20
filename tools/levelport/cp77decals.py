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
"""CP2077's projected decals (worldStaticDecalNode: grime, dirt, stains, cracks, puddles, graffiti, road markings) as a
scene's maps/<map>.xdcl and a decal image pack (format version 1, with the renderer's four conventions): rd-vulkan projects each record's
box onto the depth buffer into its DBuffer.

A decal node names a material instance (.mi) whose chain ends in one of 13 templates (decal.remt, decal_normal.remt,
decal_normal_roughness(_metalness)(_2).mt, decal_roughness.mt, decal_parallax.mt, decal_puddle.mt,
decal_gradientmap_recolor(_emissive).mt, decal_emissive_color.mt, decal_terrain_projected.mt; the road markings are
.mi parents over decal_terrain_projected). Each material is resolved through its chain (the template's defaults, then
each parent, then the child) and turned into a material record and up to three images:
  albedo RGBA  the colour (sRGB) and its coverage in A: DiffuseTexture's RGB and alpha, or white and the texture's red
               as the mask (DiffuseTextureAsMaskTexture; the colour is then the tint), the GradientMap recolour (the
               grey's value picks the ramp's colour and the ramp's alpha is the coverage, times MaskTexture), the
               terrain mask between AlphaMaskBlackPoint and WhitePoint, AlphaMaskContrast baked in
  normal       NormalTexture's RG as stored (DirectX: +G toward +V, the box's +Y below), B the parallax height
  rough        R roughness (RoughnessTexture), G metalness (MetalnessTexture), A the coverage when the albedo writes
               nothing (flag 8)
  emissive     (v2) the glow: EmissiveGradientMap by the grey's value, or Emissive x EmissiveColor; A the coverage
  tiled        (v2) decal_terrain_projected's BaseColor, Normal and Roughness, tiled world-planar every
               TERRAIN_CELL_M / MaterialTiling metres as CP2077's terrain tiles its own layers
The node gives the box (Position, Orientation, Scale: a unit cube [-0.5, 0.5]^3 scaled, measured on Watson's chained
road lines, which butt at their mean length), alpha, hide distance, flips, normal threshold, order, roughness scale
and colour scale. CP2077's box has +Z out of the decorated surface (ground decals: up) and +Y toward the top of the
image (wall text reads with flips 0 and +Y up), so the map keeps Z and negates Y: box +Y then runs down the image
(V = 0 at the first row) and the normal image's DirectX green is that +Y as stored.

    python -I cp77decals.py needs <work> <decal root> [--out <list>]   the depot paths still to fetch
    python -I cp77decals.py fetch <work> <decal root>                  fetch them (wk_fetch.py) until none are new
    python -I cp77decals.py <work> <decal root> <builds folder> [--map cp_watson] [--png <folder>]
writes <builds folder>/<map>_decals-<n>/ (a new number; built in a .partial folder) with zz_<map>_decals.pk3 (maps/<map>
.xdcl and textures/cp77decals/*.dds, stored), <map>.xdcl.json (materials, images, counts) and <map>_decals_sample.json
(records near Misty's, CP -1550 1181, for the renderer's tests)."""
import argparse
import collections
import hashlib
import json
import math
import os
import struct
import sys
import time
import zipfile
from concurrent.futures import ProcessPoolExecutor, ThreadPoolExecutor
from pathlib import Path

import numpy as np
from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent))
import cp2gltf  # noqa: E402
import cp77mat  # noqa: E402
import wk_fetch  # noqa: E402

SL = chr(92)
UPM = 37.5
NONE = 0xFFFFFFFF
MAX_PX = 1024
IMG_DIR = 'textures/cp77decals/'
# material flags
MF_EMISSIVE, MF_PUDDLE, MF_TERRAIN, MF_ROUGH_COVER = 1, 2, 4, 8
SLOTS = ('albedo', 'normal', 'rough', 'emissive_img', 'tiled_colour', 'tiled_normal', 'tiled_rough')
# decal flags
DF_FLIP_U, DF_FLIP_V, DF_THRESHOLD, DF_REORIENT = 1, 2, 4, 8
RECEIVERS = {'ROT_Static': 256, 'ROT_Road': 512, 'ROT_Terrain': 1024, 'ROT_Vehicle': 2048, 'ROT_Character': 4096}
ALL_STATIC = 256 | 512 | 1024
VERSION = 2
HEAD, IMG, DEC = 32, 64, 80
MAT_V1 = '<4I4f4f3fff3f'                 # version 1: 80 B
MAT_FMT = '<4I4f4f3fff4If3f2ff'          # version 2: v1's first 68 B, then the emissive and tiled slots: 112 B
MAT = {1: 80, 2: 112}
DEC_FMT = '<12fIffIfiff'
assert struct.calcsize(MAT_V1) == MAT[1] and struct.calcsize(MAT_FMT) == MAT[2] and struct.calcsize(DEC_FMT) == DEC
# the terrain's own texture period: CP2077's terrain cells map their UV over their own size (UVGenScaleOffset 1/136
# on 226 of 400 Watson cells, 1/129-1/134 on the rest) and multilayer_terrain.mlsetup tiles each layer matTile times
# over it (mud 120, rock debris 160, cliff 40: decal_terrain_projected's MaterialTiling takes the same values for the
# same textures, and its MicroblendTiling is relative where the terrain's mbTile is absolute); a period in metres is
# TERRAIN_CELL_M / (MaterialTiling x LayerTiling)
TERRAIN_CELL_M = 136.0

STANDARD = {'decal.remt', 'decal_normal.remt', 'decal_normal_roughness.mt', 'decal_normal_roughness_metalness.mt',
            'decal_normal_roughness_metalness_2.mt', 'decal_roughness.mt', 'decal_parallax.mt', 'decal_puddle.mt',
            'decal_gradientmap_recolor.mt', 'decal_gradientmap_recolor_emissive.mt'}
TEMPLATES = STANDARD | {'decal_emissive_color.mt', 'decal_terrain_projected.mt'}
FLAT_NORMALS = ('small_flat_normal.xbm', SL + 'normal.xbm')
MISTY = (-1550.0, 1181.0)


class Unresolved(Exception):
    pass


# ---------------------------------------------------------------------------------------------------- inputs

def depot_of_archive(name):
    """A serialised file's depot path from its header's ArchiveFileName (WolvenKit writes the file it serialised:
    <fetch>/raw/<depot path>), lower case"""
    parts = name.replace('/', SL).split(SL)
    i = next((k for k, p in enumerate(parts) if p.lower() in ('base', 'engine', 'ep1')), None)
    return SL.join(parts[i:]).lower() if i is not None else ''


class Library:
    """The fetched materials and templates (WolvenKit convert serialize: flat file names, one name collision among the
    .mi). A depot path resolves by the serialised file's own header (ArchiveFileName), else, as cp77fx.serialised()
    does, by a nested path or a flat name that only one file has."""

    def __init__(self, root):
        self.by_depot, self.by_name = {}, collections.defaultdict(list)
        for f in sorted(Path(root).glob('*.json')):
            self.by_name[f.name[:-5].lower()].append(f)
            with open(f, 'rb') as fh:
                head = fh.read(4096).decode('utf-8', 'replace')
            k = head.find('"ArchiveFileName"')
            if k >= 0:
                v = head[head.find('"', head.find(':', k) + 1) + 1:]
                v = v[:v.find('"')].encode().decode('unicode_escape')
                d = depot_of_archive(v)
                if d:
                    self.by_depot[d] = f
        self.root = Path(root)
        self.headed = set(self.by_depot.values())
        self.cache = {}

    def find(self, depot):
        d = depot.lower()
        if d in self.by_depot:
            return self.by_depot[d]
        nested = self.root / (d.replace(SL, '/') + '.json')
        if nested.exists():
            return nested
        # (a flat name whose file's header names another depot path is not this one: the collision's loser)
        same = [f for f in self.by_name.get(d.split(SL)[-1], []) if f not in self.headed]
        return same[0] if len(same) == 1 else None

    def root_chunk(self, depot):
        if depot not in self.cache:
            f = self.find(depot)
            self.cache[depot] = json.load(open(f, encoding='utf-8'))['Data']['RootChunk'] if f else None
        return self.cache[depot]


def param_value(x):
    """A material value as plain data: a texture its depot path, others as serialised (Color, Vector4, float)"""
    if isinstance(x, dict) and 'DepotPath' in x:
        return cp2gltf.depot(x)
    return x


def template_defaults(rc):
    """A .mt / .remt's parameters' defaults by name"""
    out = {}
    for group in (rc.get('parameters') or {}).get('Elements') or []:
        for h in group:
            d = h.get('Data') or {}
            n = cp2gltf.cname(d.get('parameterName'))
            t = d.get('$type', '')
            if 'texture' in d:
                out[n] = cp2gltf.depot(d['texture'])
            elif 'color' in d:
                out[n] = d['color']
            elif 'scalar' in d:
                out[n] = d['scalar']
            elif 'vector' in d:
                out[n] = d['vector']
            elif t:
                out.setdefault(n, None)
    return out


def resolve(lib, mi):
    """(template file name, parameters, chain of depot paths) of a material instance: the template's defaults, each
    parent's values over them, the child's last"""
    chain, layers, cur = [], [], mi
    for _ in range(8):
        rc = lib.root_chunk(cur)
        if rc is None:
            raise Unresolved('not fetched: ' + cur)
        chain.append(cur)
        if cur.lower().endswith(('.mt', '.remt')):
            params = template_defaults(rc)
            for vals in reversed(layers):
                params.update(vals)
            return cur.split(SL)[-1].lower(), params, chain
        vals = {}
        for v in rc.get('values') or []:
            for k, x in v.items():
                if k != '$type':
                    vals[k] = param_value(x)
        layers.append(vals)
        cur = cp2gltf.depot(rc.get('baseMaterial'))
        if not cur:
            raise Unresolved('no base material: ' + chain[-1])
    raise Unresolved('chain too long: ' + mi)


class DecalTextures(cp77mat.Materials):
    """cp77mat's texture loading (LRU of float images, capped sizes) over the archive PNGs (<png>/<depot>.png) and the
    textures embedded in the .mi files (<embedded>/png/<sha1(depot)[:10]>_<name>.png, map.json naming each)"""

    def __init__(self, png, embedded):
        super().__init__(png, size=MAX_PX)
        self.embedded = {}
        m = Path(embedded) / 'map.json'
        if m.exists():
            for flat, dp in json.load(open(m, encoding='utf-8')).items():
                self.embedded[dp.lower()] = Path(embedded) / 'png' / (flat.rsplit('.', 1)[0] + '.png')
        self.by_stem = collections.defaultdict(list)
        for p in Path(png).rglob('*.png'):
            self.by_stem[p.stem.lower()].append(p)
        self.found = collections.Counter()

    def png(self, depot):
        if not depot or not isinstance(depot, str):
            return None
        p = self.root / (depot.replace(SL, '/').rsplit('.', 1)[0] + '.png')
        if p.exists():
            return p
        e = self.embedded.get(depot.lower())
        if e is not None and e.exists():
            return e
        same = self.by_stem.get(depot.split(SL)[-1].rsplit('.', 1)[0].lower(), [])
        return same[0] if len(same) == 1 else p

    def get(self, depot, mode='RGBA'):
        """A texture as float (H, W, C) at most MAX_PX, or None"""
        return self.tex(depot, mode, MAX_PX)


# ---------------------------------------------------------------------------------------------------- materials

def fnum(v, default=0.0):
    try:
        return float(v) if v is not None else float(default)
    except (TypeError, ValueError):
        return float(default)


def color_of(v, default=(255, 255, 255, 255)):
    """A Color parameter as (r, g, b, a) bytes"""
    if not isinstance(v, dict):
        return tuple(float(x) for x in default)
    return tuple(fnum(v.get(k), d) for k, d in zip(('Red', 'Green', 'Blue', 'Alpha'), default))


def vec4(v, default=(0.0, 0.0, 0.0, 0.0)):
    if not isinstance(v, dict):
        return tuple(default)
    return tuple(fnum(v.get(k), d) for k, d in zip('XYZW', default))


def contrast(a, c):
    """AlphaMaskContrast (-1..1) on a coverage: > 0 narrows the ramp about 0.5 (c -> 1 a hard cut), < 0 flattens it"""
    if abs(c) < 1e-6:
        return a
    k = 1.0 / max(1.0 - c, 1e-3) if c > 0 else 1.0 + c
    return np.clip((a - 0.5) * k + 0.5, 0.0, 1.0)


def resize_to(a, shape):
    """A float (H, W[, C]) image resampled to (h, w)"""
    if a.shape[:2] == tuple(shape):
        return a
    ch = a[..., None] if a.ndim == 2 else a
    out = np.stack([np.asarray(Image.fromarray(np.ascontiguousarray(ch[..., c], np.float32)).resize((shape[1], shape[0]),
                                                                                    Image.Resampling.BILINEAR))
                    for c in range(ch.shape[2])], -1)
    return out[..., 0] if a.ndim == 2 else out


def gradient_lookup(g, value):
    """A gradient map (float RGBA) sampled along its middle at u = value as the GPU does (bilinear, clamped): (rgb, a)"""
    h, w = g.shape[:2]
    row = g[h // 2] if h % 2 else (g[h // 2 - 1] + g[h // 2]) * 0.5
    x = np.clip(value * w - 0.5, 0.0, w - 1.0)
    i0 = np.floor(x).astype(np.int64)
    i1 = np.minimum(i0 + 1, w - 1)
    f = (x - i0)[..., None]
    c = row[i0] * (1 - f) + row[i1] * f
    return c[..., :3], c[..., 3]


def uv_rect(t, p):
    """The material's UV rectangle (u0 v0 u1 v1; V = 0 at the image's first row): SubRegion of AtlasSize pixels (x, y,
    width, height) or SubUVx x SubUVy cells, Frame row-major from the top left"""
    if 'SubRegion' in p and t in ('decal_normal_roughness_metalness_2.mt', 'decal_parallax.mt'):
        x, y, w, h = vec4(p.get('SubRegion'))
        aw, ah = vec4(p.get('AtlasSize'), (1024.0, 1024.0, 0, 0))[:2]
        aw, ah = aw if aw > 0 else 1024.0, ah if ah > 0 else 1024.0
        # (y counts from the image's bottom: entropy_01_letter_a's SubRegion 584 668 146 171 is the A, rows 185-356
        # of the 1024 atlas from its top)
        # (clamped: corpo_kang_tao_logo_01's reaches 3 px past the atlas's top)
        return [float(np.clip(v, 0.0, 1.0)) for v in (x / aw, 1.0 - (y + h) / ah, (x + w) / aw, 1.0 - y / ah)]
    cols, rows = fnum(p.get('SubUVx'), 1.0), fnum(p.get('SubUVy'), 1.0)
    cols, rows = cols if cols > 0 else 1.0, rows if rows > 0 else 1.0
    frame = max(int(round(fnum(p.get('Frame'), 0.0))), 0)
    nc, nr = max(int(round(cols)), 1), max(int(round(rows)), 1)
    ci, ri = frame % nc, (frame // nc) % nr
    return [ci / cols, ri / rows, (ci + 1) / cols, (ri + 1) / rows]


def lin_rgb(c):
    """A Color parameter's RGB (sRGB bytes, as an artist picks it) in linear"""
    return cp77mat.lin(np.array(c[:3], np.float64) / 255.0)


def u8(a):
    return np.uint8(np.round(np.clip(a, 0.0, 1.0) * 255.0))


def default_tex(depot):
    return not depot or depot.lower().startswith('engine' + SL)


class Converter:
    """Material instances -> (material record, images); images keep one DDS per distinct recipe"""

    def __init__(self, lib, tex, pack, side_dir, workers):
        self.lib, self.tex, self.pack, self.side_dir = lib, tex, pack, Path(side_dir)
        self.images = {}            # name -> index
        self.recipe_of = {}         # name -> recipe key
        # images encode on a pool as they are made and go into the pack from this thread, oldest first:
        # the pixels are not held (a thousand 1024 px RGBA images are 4 GB)
        self.pool, self.workers, self.pending = ThreadPoolExecutor(workers), workers, collections.deque()
        self.bytes = collections.Counter()
        self.materials, self.mat_index, self.meta = [], {}, []
        self.failed = {}            # mi -> reason
        self.notes = collections.defaultdict(list)    # template -> notes
        self.by_template = collections.Counter()
        self.side_images = []       # images kept beside the pack: none since v2 (the summary still lists them)
        self.resolved = {}

    # ---- images

    def image(self, recipe, stem, arr, bc7=True):
        """An image's index (its name: cp2gltf.cache_name of the source depot path for a texture taken as it is, else
        a hash of its recipe; at most 63 bytes with the NUL)"""
        key = recipe[1] if recipe[2:] == ('plain',) else repr(recipe)
        h = hashlib.sha1(key.lower().encode()).hexdigest()[:10]
        stem = stem.split(SL)[-1].rsplit('.', 1)[0].lower()
        name = (h + '_' + stem)[:63 - len(IMG_DIR) - len('.dds')]
        path = IMG_DIR + name + '.dds'
        if path in self.recipe_of and self.recipe_of[path] != key:
            raise RuntimeError('image name collision: %s (%s, %s)' % (path, key, self.recipe_of[path]))
        if path not in self.recipe_of:
            self.recipe_of[path] = key
            self.pending.append((path, self.pool.submit(cp2gltf.dds_bytes, np.ascontiguousarray(arr), bc7)))
            while len(self.pending) > 3 * self.workers:
                self.drain(1)
            self.images[path] = len(self.images)
        return self.images[path]

    # ---- materials

    def material(self, mi, scale_rgb):
        """The material index for a node's material and colour scale (the scale's RGB multiplies the tint, so a material
        exists per distinct node colour), or a str: why there is none"""
        key = (mi.lower(), scale_rgb)
        if key in self.mat_index:
            return self.mat_index[key]
        base = self.base(mi)
        if isinstance(base, str):
            self.mat_index[key] = base
            return base
        rec = dict(base['rec'])
        rec['tint'] = [float(t * s) for t, s in zip(rec['tint'], scale_rgb)]
        self.materials.append(rec)
        self.meta.append(dict(base['meta'], colour_scale=list(scale_rgb)))
        self.mat_index[key] = len(self.materials) - 1
        return self.mat_index[key]

    def base(self, mi):
        """A material instance's record before the node's colour scale, or a str (the reason it has none)"""
        if mi.lower() in self.resolved:
            return self.resolved[mi.lower()]
        try:
            t, p, chain = resolve(self.lib, mi)
            if t not in TEMPLATES:
                raise Unresolved('template not a projected decal: ' + t)
            got = self.convert(t, p)
            if isinstance(got, str):
                raise Unresolved(got)
            rec, meta = got
            meta.update(mi=mi, template=t, chain=chain)
            self.by_template[t] += 1
            out = {'rec': rec, 'meta': meta}
        except Unresolved as e:
            self.failed[mi] = str(e)
            out = str(e)
        self.resolved[mi.lower()] = out
        return out

    def convert(self, t, p):
        rec = {'albedo': NONE, 'normal': NONE, 'rough': NONE, 'flags': 0, 'albedo_alpha': 0.0, 'normal_alpha': 0.0,
               'rough_alpha': 0.0, 'rough_bias': fnum(p.get('RoughnessBias')), 'uv': [0.0, 0.0, 1.0, 1.0],
               'tint': [1.0, 1.0, 1.0], 'emissive': 0.0, 'height': 0.0,
               'emissive_img': NONE, 'tiled_colour': NONE, 'tiled_normal': NONE, 'tiled_rough': NONE, 'period': 0.0,
               'tiled_scale': [1.0, 1.0, 1.0], 'levels': [0.0, 1.0]}
        meta = {}
        if t == 'decal_terrain_projected.mt':
            return self.terrain(p, rec, meta)
        if t == 'decal_emissive_color.mt':
            return self.emissive_color(p, rec, meta)
        return self.standard(t, p, rec, meta)

    def standard(self, t, p, rec, meta):
        dt = p.get('DiffuseTexture')
        D = self.tex.get(dt)
        if D is None:
            return 'diffuse texture missing: %s' % dt
        cov_c = fnum(p.get('AlphaMaskContrast'))
        as_mask = fnum(p.get('DiffuseTextureAsMaskTexture')) > 0.5
        gradient = t.startswith('decal_gradientmap_recolor')
        if gradient:
            G = self.tex.get(p.get('GradientMap'))
            if G is None:
                return 'gradient map missing: %s' % p.get('GradientMap')
            lum = D[..., 0]
            rgb, cov = gradient_lookup(G, lum)
            mt = p.get('MaskTexture')
            mask_used = bool(mt) and not mt.lower().endswith(('small_white.xbm', SL + 'white.xbm'))
            if mask_used:
                M = self.tex.get(mt)
                if M is None:
                    return 'mask texture missing: %s' % mt
                cov = cov * resize_to(M[..., 0], cov.shape)
            recipe = ('albedo', dt, p.get('GradientMap'), mt if mask_used else '', round(cov_c, 4))
        elif as_mask:
            rgb, cov = np.ones(D.shape[:2] + (3,), np.float32), D[..., 0]
            recipe = ('albedo', dt, 'mask', round(cov_c, 4))
        else:
            rgb, cov = D[..., :3], D[..., 3]
            recipe = ('albedo', dt, round(cov_c, 4)) if abs(cov_c) > 1e-6 else ('albedo', dt, 'plain')
        cov = contrast(cov, cov_c).astype(np.float32)
        col = color_of(p.get('DiffuseColor'))
        rec['tint'] = lin_rgb(col).tolist()
        # (DiffuseColor's alpha is not an opacity: every decal_normal_roughness_metalness_2 material has 0 there, the
        # black entropy_01 letters too, and dirt_b_dark is 43 9 9 0; the template's DiffuseAlpha is, where it has one)
        rec['albedo_alpha'] = fnum(p.get('DiffuseAlpha'), 1.0) if 'DiffuseAlpha' in p else 1.0
        rec['uv'] = uv_rect(t, p)
        if t == 'decal_puddle.mt':
            rec['flags'] |= MF_PUDDLE

        # normal (and the parallax height in its B)
        nt = p.get('NormalTexture')
        height = None
        if t == 'decal_parallax.mt' and not default_tex(p.get('HeightTexture')):
            H = self.tex.get(p.get('HeightTexture'))
            if H is not None and fnum(p.get('HeightStrength')) > 0:
                height = H[..., 0]
                rec['height'] = fnum(p.get('HeightStrength'))
        normal = N = None
        if nt and not nt.lower().endswith(FLAT_NORMALS):
            N = self.tex.get(nt)
            if N is None:
                self.notes[t].append('normal texture missing: normal left out')
        if N is not None:
            normal = np.zeros(N.shape[:2] + (4,), np.float32)
            normal[..., :2] = N[..., :2]                # RG as stored (DirectX: +G is +V, the box's +Y)
            normal[..., 3] = 1.0
            if height is not None:
                normal[..., 2] = resize_to(height, N.shape[:2])
        elif height is not None:
            self.notes[t].append('height without a normal map: height dropped (no slot)')
            rec['height'], height = 0.0, None

        # roughness / metalness
        rt, mt_ = p.get('RoughnessTexture'), p.get('MetalnessTexture')
        rough = None
        if 'RoughnessTexture' in p or 'MetalnessTexture' in p:
            R = self.tex.get(rt, 'L') if rt else None
            Mt = self.tex.get(mt_, 'L') if mt_ else None
            if (rt and R is None) or (mt_ and Mt is None):
                self.notes[t].append('roughness or metalness texture missing: its default (0.5 / 0) instead')
            shp = max([a.shape[:2] for a in (R, Mt) if a is not None] + [(4, 4)], key=lambda s: s[0] * s[1])
            rough = np.zeros(shp + (4,), np.float32)
            rough[..., 0] = resize_to(R[..., 0], shp) if R is not None else 0.5
            rough[..., 1] = resize_to(Mt[..., 0], shp) if Mt is not None else 0.0
            rough[..., 3] = 1.0
        writes_albedo = rec['albedo_alpha'] > 0
        if not writes_albedo and normal is None and rough is None:
            return 'material writes nothing (albedo alpha 0, no normal or roughness)'
        if float(cov.max()) <= 0.0:
            return 'material writes nothing (coverage 0 everywhere)'

        # the images, now the material is known to stand
        if normal is not None:
            nrec = ('normal', nt, p.get('HeightTexture')) if height is not None else ('normal', nt, 'plain')
            rec['normal'] = self.image(nrec, nt, u8(normal))
            rec['normal_alpha'] = 1.0
        if rough is not None:
            rec['rough_alpha'] = fnum(p.get('RoughnessMetalnessAlpha'), 1.0)
        if not writes_albedo and rough is not None:
            # the colour writes nothing: the coverage rides in the rough image's A (flag 8), no albedo image
            shp = cov.shape if cov.size >= rough.shape[0] * rough.shape[1] else rough.shape[:2]
            rough = resize_to(rough, shp)
            rough[..., 3] = resize_to(cov, shp)
            rec['flags'] |= MF_ROUGH_COVER
            rec['rough'] = self.image(('rough', rt, mt_, recipe), rt or mt_, u8(rough), True)
        else:
            # (albedo alpha 0 with a normal and no rough image: the albedo image stays as the coverage)
            rec['albedo'] = self.image(recipe, dt, u8(np.concatenate([rgb, cov[..., None]], -1)))
            if rough is not None:
                rec['rough'] = self.image(('rough', rt, mt_ or '', 'rm'), rt or mt_, u8(rough), False)
        if t == 'decal_gradientmap_recolor_emissive.mt':
            ev = fnum(p.get('EmissiveEV'))
            eg = p.get('EmissiveGradientMap')
            E = None if default_tex(eg) else self.tex.get(eg)
            if E is not None:
                em, _ = gradient_lookup(E, D[..., 0])
                if float(em.max()) > 1e-3:
                    # (v2) the glow's own image: EmissiveGradientMap's colour by the grey's value, the coverage in A
                    rec['emissive_img'] = self.image(('emissive', dt, eg), eg,
                                                     u8(np.concatenate([em, cov[..., None]], -1)))
                    rec['emissive'] = float(2.0 ** (ev - 5.0))
                    rec['flags'] |= MF_EMISSIVE
        return rec, meta

    def terrain(self, p, rec, meta):
        """decal_terrain_projected (terrain patches and the road markings): AlphaMask between its black and white
        points as the coverage (the albedo image, white); BaseColor, Normal and Roughness tile world-planar as the
        terrain's own layers do, every TERRAIN_CELL_M / (MaterialTiling x LayerTiling) metres (v2's tiled slots):
          colour  BaseColor as it is, its scale 2 x BaseColorScale (the mltemplates' colour textures are detail centred
                  on 0.5 linear: cp77mat), the tint 1; a default (engine) BaseColor has no tiled image, its
                  BaseColorScale the tint as in v1
          normal  Normal's RG with NormalStrength baked about 0.5
          rough   Roughness through RoughnessLevels' input range (X high, Y low), the output range (W low, Z high) the
                  levels: (1 0 1 0) and SpecularIntensityLevels' default (0 1 0 1) are both the identity in this
                  reading, and ColorMaskLevels 1 0 1 1 is a constant 1, not a degenerate step
        Microblend, SpecularIntensity and ColorMaskLevels are not carried."""
        am = p.get('AlphaMask')
        A = self.tex.get(am)
        if A is None:
            return 'alpha mask missing: %s' % am
        b, w = fnum(p.get('AlphaMaskBlackPoint'), 0.0), fnum(p.get('AlphaMaskWhitePoint'), 1.0)
        cov = np.clip((A[..., 0] - b) / max(w - b, 1e-4), 0.0, 1.0)
        img = np.concatenate([np.ones(cov.shape + (3,), np.float32), cov[..., None]], -1)
        rec['albedo'] = self.image(('albedo', am, 'terrain', round(b, 4), round(w, 4)), am, u8(img))
        rec['albedo_alpha'] = 1.0
        tiling = fnum(p.get('MaterialTiling'), 100.0) * fnum(p.get('LayerTiling'), 1.0)
        rec['period'] = TERRAIN_CELL_M / max(tiling, 1e-3)
        scale = np.array(vec4(p.get('BaseColorScale'), (0.5, 0.5, 0.5, 0))[:3])
        bc = p.get('BaseColor')
        C = None if default_tex(bc) else self.tex.get(bc, 'RGB')
        if C is not None:
            rgba = np.concatenate([C, np.ones(C.shape[:2] + (1,), np.float32)], -1)
            rec['tiled_colour'] = self.image(('tiled_colour', bc), bc, u8(rgba))
            rec['tiled_scale'] = (2.0 * scale).tolist()
            rec['tint'] = [1.0, 1.0, 1.0]
        else:
            if bc and not default_tex(bc):
                self.notes['decal_terrain_projected.mt'].append('BaseColor missing: BaseColorScale as the tint')
            rec['tint'] = scale.tolist()
        nt = p.get('Normal')
        N = None if (not nt or nt.lower().endswith(FLAT_NORMALS)) else self.tex.get(nt)
        if N is not None:
            k = fnum(p.get('NormalStrength'), 1.0)
            n = np.zeros(N.shape[:2] + (4,), np.float32)
            n[..., :2] = (N[..., :2] - 0.5) * k + 0.5          # (strength about the stored zero; RG otherwise raw)
            n[..., 3] = 1.0
            rec['tiled_normal'] = self.image(('tiled_normal', nt, round(k, 4)), nt, u8(n))
            rec['normal_alpha'] = 1.0
        rt = p.get('Roughness')
        R = None if not rt else self.tex.get(rt, 'L')
        if R is not None:
            hi, lo, out_hi, out_lo = vec4(p.get('RoughnessLevels'), (1.0, 0.0, 1.0, 0.0))
            r = R[..., 0]
            r = np.clip((r - lo) / (hi - lo), 0.0, 1.0) if abs(hi - lo) > 1e-4 else np.where(r >= hi, 1.0, 0.0)
            rough = np.zeros(r.shape + (4,), np.float32)
            rough[..., 0], rough[..., 3] = r, 1.0
            rec['tiled_rough'] = self.image(('tiled_rough', rt, round(hi, 4), round(lo, 4)), rt, u8(rough), False)
            rec['levels'] = [out_lo, out_hi]
            rec['rough_alpha'] = 1.0
        rec['flags'] |= MF_TERRAIN
        meta['tiling'] = tiling
        self.notes['decal_terrain_projected.mt'].append('microblend, SpecularIntensity and ColorMaskLevels dropped')
        return rec, meta

    def emissive_color(self, p, rec, meta):
        """decal_emissive_color: Emissive's colour and value (as coverage, cut at EmissiveAlphaThreshold) glowing in
        EmissiveColor at EmissiveEV on the lamps' scale (2^(EV - 5)); the template writes no albedo of its own: v2's
        emissive image alone (flag 1)"""
        et = p.get('Emissive')
        E = self.tex.get(et)
        if E is None or default_tex(et):
            return 'emissive texture missing: %s' % et
        thr = fnum(p.get('EmissiveAlphaThreshold'), 0.005)
        v = E[..., :3].max(-1)
        cov = np.where(v > thr, v, 0.0)
        # (v2) the glow in the emissive slot, EmissiveColor baked in (linear), the coverage in A; no albedo
        ec = lin_rgb(color_of(p.get('EmissiveColor')))
        glow = cp77mat.srgb(cp77mat.lin(E[..., :3]) * ec[None, None, :].astype(np.float32))
        rec['emissive_img'] = self.image(('emissive', et, round(thr, 4), tuple(np.round(ec, 5).tolist())), et,
                                         u8(np.concatenate([glow, cov[..., None]], -1)))
        rec['emissive'] = float(2.0 ** (fnum(p.get('EmissiveEV')) - 5.0))
        rec['uv'] = uv_rect('decal_emissive_color.mt', p)
        rec['flags'] |= MF_EMISSIVE
        return rec, meta

    # ---- images out

    def drain(self, n=None):
        """Write the oldest n encoded images (all: None) into the pack"""
        while self.pending and (n is None or n > 0):
            path, fut = self.pending.popleft()
            dds = fut.result()
            self.pack.writestr(zipfile.ZipInfo(path), dds, compress_type=zipfile.ZIP_STORED)
            self.bytes['pack'] += len(dds)
            n = None if n is None else n - 1

    def close(self):
        self.drain()
        self.pool.shutdown()


# ---------------------------------------------------------------------------------------------------- nodes

def _init_worker(work, variants):
    cp2gltf._SHOWN_VARIANTS[str(work)] = variants


def sector_decals(args):
    """A sector's visible decal records: [(mi, pos, quat, scale, node fields)], and its counts"""
    work, f = args
    stats = collections.Counter()
    data = open(f, 'rb').read()
    if b'worldStaticDecalNode' not in data:
        return [], stats
    rc = json.loads(data)['Data']['RootChunk']
    nodes = rc['nodes']
    out = []
    sector = Path(f).name.split('.')[0]
    for r in cp2gltf.shown_records(work, sector, rc):         # variant-off ranges stay out
        n = nodes[r['NodeIndex']]['Data']
        if n.get('$type') != 'worldStaticDecalNode':
            continue
        if not n.get('isVisibleInGame', 1):
            stats['hidden in game'] += 1
            continue
        cs = n.get('diffuseColorScale') or {}
        out.append({
            'mi': cp2gltf.depot(n.get('material')), 'sector': sector,
            'pos': [float(r['Position'].get(k, 0.0)) for k in 'XYZ'],
            'quat': {k: float((r.get('Orientation') or {}).get(k, 1.0 if k == 'r' else 0.0)) for k in 'ijkr'},
            'scale': [float((r.get('Scale') or {}).get(k, 1.0)) for k in 'XYZ'],
            'alpha': fnum(n.get('alpha'), 1.0),
            'hide': fnum(n.get('forcedAutoHideDistance')) or fnum(n.get('autoHideDistance')),
            'stream': cp2gltf.stream_of(r),
            'flip_u': bool(n.get('horizontalFlip')), 'flip_v': bool(n.get('verticalFlip')),
            'thr_on': bool(n.get('enableNormalTreshold', 1)), 'thr': fnum(n.get('normalThreshold')),
            'reorient': n.get('normalsBlendingMode') == 'Reorient',
            'mode': n.get('decalRenderMode') or 'DRM_AllStatic', 'surface': n.get('surfaceType') or 'ROT_Static',
            'order': int(n.get('orderNo') or 0), 'rough_scale': fnum(n.get('roughnessScale'), 1.0),
            'cs': (round(fnum(cs.get('Red'), 1.0), 4), round(fnum(cs.get('Green'), 1.0), 4),
                   round(fnum(cs.get('Blue'), 1.0), 4)),
            'cs_alpha': fnum(cs.get('Alpha'), 1.0),
            'stretch': bool(n.get('isStretchingEnabled')),
        })
    return out, stats


def read_nodes(work, workers, pattern='*'):
    files = sorted((Path(work) / 'json').glob(pattern + '.streamingsector.json'))
    variants = cp2gltf.sector_variants(Path(work)) or {}
    out, stats = [], collections.Counter()
    with ProcessPoolExecutor(workers, initializer=_init_worker, initargs=(str(work), variants)) as ex:
        for got, st in ex.map(sector_decals, [(str(work), str(f)) for f in files], chunksize=16):
            out += got
            stats.update(st)
    return out, stats, len(files)


# ---------------------------------------------------------------------------------------------------- fetch

# what a material names that the converter reads: parents and templates (whose defaults name textures), the
# terrain-projected materials' multilayer setups and templates, and textures
REF_SUFFIXES = ('.xbm', '.mt', '.remt', '.mi', '.mlsetup', '.mltemplate')


def fetch_roots(work, workers, area=None):
    """The material instances the visible decal nodes name (every sector, or the area's circle)"""
    nodes, _, _ = read_nodes(work, workers)
    if area:
        cp2gltf.use_area(area)
        c, r = cp2gltf.AREAS[area]['centre'], cp2gltf.AREAS[area]['radius']
        nodes = [d for d in nodes if cp2gltf.in_area(d['pos'][0], d['pos'][1], c, r, d['pos'][2])]
    return sorted({wk_fetch.norm(d['mi']) for d in nodes if d['mi'] and not d['mi'].isdigit()})


def needs(roots, root, retry=False):
    """What the decal fetch at root still lacks: from the nodes' materials through every fetched material, parent,
    template and multilayer file to the textures they name (a census of materials, lists and missing files as
    one walk). Resolved as the converter resolves (Library, DecalTextures); a texture embedded in a fetched file
    is the embedded export's, not the archives'. Returns {'need': [...], 'absent': [...] (needed but listed in
    fetch_absent.txt; retry: none are), 'embedded': [...] (embedded keys without a png), 'counts': {...}}"""
    root = Path(root)
    lib = Library(root / 'json')
    tex = DecalTextures(root / 'png', root / 'embedded')
    absent = set() if retry else wk_fetch.Fetched(root).absent
    every, emb_todo = wk_fetch.embedded_todo(root)
    embedded = {wk_fetch.norm(v) for v in every.values()}
    seen, todo, need = set(), list(roots), set()
    counts = collections.Counter()
    while todo:
        p = todo.pop()
        if p in seen:
            continue
        seen.add(p)
        counts[p.rsplit('.', 1)[-1]] += 1
        if p.endswith('.xbm'):
            if p in embedded:
                counts['embedded'] += 1
            elif not tex.png(p).exists():
                need.add(p)
            continue
        rc = lib.root_chunk(p)
        if rc is None:
            need.add(p)
            continue
        todo += sorted(wk_fetch.walk_refs(rc, REF_SUFFIXES, set()) - seen)
    return {'need': sorted(need - absent), 'absent': sorted(need & absent), 'embedded': sorted(emb_todo),
            'counts': dict(counts)}


def fetch_loop(work, root, workers, area=None, retry=False, rounds=8):
    """Fetch until nothing new is needed: the nodes' materials, then what they name, then what those name, the
    embedded textures after each round (retry: the paths in fetch_absent.txt are asked for again, once)"""
    roots = fetch_roots(work, workers, area)
    print('cp77decals fetch: %d materials on the nodes' % len(roots), flush=True)
    tried, emb_tried = set(), set()
    for k in range(rounds):
        n = needs(roots, root, retry and k == 0)
        new = [p for p in n['need'] if p not in tried]
        emb = [e for e in n['embedded'] if e not in emb_tried]
        print('cp77decals fetch: round %d: %d needed (%d new), %d absent, %d embedded to export (%d new)' % (
            k + 1, len(n['need']), len(new), len(n['absent']), len(n['embedded']), len(emb)), flush=True)
        if not new and not emb:
            return n
        if new:
            wk_fetch.fetch(new, root)
            tried |= set(new)
        wk_fetch.fetch_embedded(root)
        emb_tried |= set(n['embedded'])
    n = needs(roots, root)
    print('cp77decals fetch: stopped after %d rounds, %d still needed' % (rounds, len(n['need'])), flush=True)
    return n


def box_map(d, origin):
    """The 3x4 map from scene space (game units) to the box's [-1, 1]^3: CP's unit cube [-0.5, 0.5]^3 scaled, its
    local Y negated (box +Y runs down the image, V = 0 at the first row), X and Z kept (+Z out of the surface)"""
    R = cp2gltf.quat_rot(d['quat'])
    S = np.array(d['scale'], float)
    D = np.diag([2.0 / S[0], -2.0 / S[1], 2.0 / S[2]])
    A = D @ R.T
    M = np.zeros((3, 4))
    M[:, :3] = A / UPM
    M[:, 3] = A @ (np.asarray(origin, float) - np.array(d['pos'], float))
    return M


def receivers(d):
    if d['mode'] == 'DRM_AllStatic':
        return ALL_STATIC
    return RECEIVERS.get(d['surface'], 0)


# ---------------------------------------------------------------------------------------------------- file

def xdcl_blob(images, materials, decals):
    out = [struct.pack('<4s4I3I', b'XDCL', VERSION, len(images), len(materials), len(decals), 0, 0, 0)]
    for path in images:
        b = path.encode('ascii')
        assert len(b) < IMG, path
        out.append(b.ljust(IMG, b'\0'))
    for m in materials:
        out.append(struct.pack(MAT_FMT, m['albedo'], m['normal'], m['rough'], m['flags'], m['albedo_alpha'],
                               m['normal_alpha'], m['rough_alpha'], m['rough_bias'], *m['uv'], *m['tint'],
                               m['emissive'], m['height'], m['emissive_img'], m['tiled_colour'], m['tiled_normal'],
                               m['tiled_rough'], m['period'], *m['tiled_scale'], *m['levels'], 0.0))
    for d in decals:
        out.append(struct.pack(DEC_FMT, *d['map'], d['mat'], d['alpha'], d['hide'], d['flags'], d['thr'], d['order'],
                               d['rough_scale'], d['cs_alpha']))
    return b''.join(out)


def read_xdcl(blob):
    """The file back (version 1 or 2): {'version', 'images': [path], 'materials': [dict], 'decals': [dict]}"""
    magic, ver, ni, nm, nd = struct.unpack_from('<4s4I', blob, 0)
    assert magic == b'XDCL' and ver in MAT, (magic, ver)
    ms = MAT[ver]
    assert len(blob) == HEAD + ni * IMG + nm * ms + nd * DEC, (len(blob), ni, nm, nd)
    at = HEAD
    images = [blob[at + k * IMG:at + (k + 1) * IMG].rstrip(b'\0').decode('ascii') for k in range(ni)]
    at += ni * IMG
    mats = []
    for k in range(nm):
        v = struct.unpack_from(MAT_V1 if ver == 1 else MAT_FMT, blob, at + k * ms)
        m = {'albedo': v[0], 'normal': v[1], 'rough': v[2], 'flags': v[3], 'albedo_alpha': v[4],
             'normal_alpha': v[5], 'rough_alpha': v[6], 'rough_bias': v[7], 'uv': list(v[8:12]),
             'tint': list(v[12:15]), 'emissive': v[15], 'height': v[16]}
        if ver == 1:
            m.update(emissive_img=NONE, tiled_colour=NONE, tiled_normal=NONE, tiled_rough=NONE, period=0.0,
                     tiled_scale=[1.0, 1.0, 1.0], levels=[0.0, 1.0], reserved=list(v[17:20]))
        else:
            m.update(emissive_img=v[17], tiled_colour=v[18], tiled_normal=v[19], tiled_rough=v[20], period=v[21],
                     tiled_scale=list(v[22:25]), levels=list(v[25:27]), reserved=[v[27]])
        mats.append(m)
    at += nm * ms
    decals = []
    for k in range(nd):
        v = struct.unpack_from(DEC_FMT, blob, at + k * DEC)
        decals.append({'map': list(v[:12]), 'mat': v[12], 'alpha': v[13], 'hide': v[14], 'flags': v[15], 'thr': v[16],
                       'order': v[17], 'rough_scale': v[18], 'cs_alpha': v[19]})
    return {'version': ver, 'images': images, 'materials': mats, 'decals': decals}


def validate(blob, pack_names=None, expect=None):
    """Parse a .xdcl back and check it: counts, image paths (in the pack), indices, flags, finite values, ranges,
    sort order, invertible maps. Returns (parsed, problems)"""
    x = read_xdcl(blob)
    bad = []
    fin = lambda vals: all(math.isfinite(v) for v in vals)
    if expect:
        for k in ('images', 'materials', 'decals'):
            if len(x[k]) != expect[k]:
                bad.append('%s: %d, expected %d' % (k, len(x[k]), expect[k]))
    for i, p in enumerate(x['images']):
        if not p.startswith(IMG_DIR) or not p.endswith('.dds'):
            bad.append('image %d: path %s' % (i, p))
        if pack_names is not None and p not in pack_names:
            bad.append('image %d: not in the pack: %s' % (i, p))
    used = set()
    for i, m in enumerate(x['materials']):
        for k in SLOTS:
            if m[k] != NONE:
                used.add(m[k])
                if m[k] >= len(x['images']):
                    bad.append('material %d: %s image %d out of range' % (i, k, m[k]))
        if m['flags'] & ~15:
            bad.append('material %d: flags %x' % (i, m['flags']))
        if m['flags'] & MF_ROUGH_COVER and m['rough'] == NONE:
            bad.append('material %d: coverage from a rough image it has not' % i)
        vals = [m['albedo_alpha'], m['normal_alpha'], m['rough_alpha'], m['rough_bias'], *m['uv'], *m['tint'],
                m['emissive'], m['height'], m['period'], *m['tiled_scale'], *m['levels']]
        if not fin(vals):
            bad.append('material %d: non-finite' % i)
        if not all(0.0 <= a <= 1.0 for a in (m['albedo_alpha'], m['normal_alpha'], m['rough_alpha'])):
            bad.append('material %d: alpha out of 0-1' % i)
        if not (-1e-6 <= min(m['uv']) and max(m['uv']) <= 1.0 + 1e-6 and m['uv'][2] > m['uv'][0]
                and m['uv'][3] > m['uv'][1]):
            bad.append('material %d: uv %s' % (i, m['uv']))
        if min(m['tint']) < 0 or m['emissive'] < 0 or m['height'] < 0:
            bad.append('material %d: negative tint / emissive / height' % i)
        if all(m[k] == NONE for k in SLOTS):
            bad.append('material %d: writes nothing' % i)
        if m['normal'] == NONE and m['tiled_normal'] == NONE and m['normal_alpha'] != 0:
            bad.append('material %d: normal alpha without an image' % i)
        if m['albedo'] == NONE and m['emissive_img'] == NONE and not m['flags'] & MF_ROUGH_COVER:
            bad.append('material %d: no coverage (no albedo, emissive or flag 8)' % i)
        if (m['flags'] & MF_EMISSIVE) != (MF_EMISSIVE if m['emissive_img'] != NONE else 0) or \
                (m['emissive_img'] != NONE) != (m['emissive'] > 0):
            bad.append('material %d: emissive flag, image and scale disagree' % i)
        tiled = any(m[k] != NONE for k in ('tiled_colour', 'tiled_normal', 'tiled_rough'))
        if tiled != bool(m['flags'] & MF_TERRAIN) or (tiled and not m['period'] > 0) or (not tiled and m['period']):
            bad.append('material %d: tiled slots, flag 4 and period disagree' % i)
        if min(m['tiled_scale']) < 0 or not (-1e-6 <= min(m['levels']) and max(m['levels']) <= 2.0):
            # (RoughnessLevels' output reaches 1.2 on terrain_asphalt_patch_old_01: the renderer clamps the result)
            bad.append('material %d: tiled scale %s levels %s' % (i, m['tiled_scale'], m['levels']))
    if len(used) != len(x['images']):
        bad.append('%d images no material uses' % (len(x['images']) - len(used)))
    prev = (-1 << 31, -1)
    for i, d in enumerate(x['decals']):
        if d['mat'] >= len(x['materials']):
            bad.append('decal %d: material %d' % (i, d['mat']))
        if not fin(d['map'] + [d['alpha'], d['hide'], d['thr'], d['rough_scale'], d['cs_alpha']]):
            bad.append('decal %d: non-finite' % i)
        if d['flags'] & ~(15 | 0x1F00) or not d['flags'] & 0x1F00:
            bad.append('decal %d: flags %x' % (i, d['flags']))
        if not (0.0 <= d['alpha'] <= 1.0 + 1e-6) or d['hide'] < 0 or not (0 <= d['thr'] <= 90):
            bad.append('decal %d: alpha %g hide %g threshold %g' % (i, d['alpha'], d['hide'], d['thr']))
        if (d['order'], d['mat']) < prev:
            bad.append('decal %d: out of order' % i)
        prev = (d['order'], d['mat'])
        A = np.array(d['map'], float).reshape(3, 4)[:, :3]
        if abs(np.linalg.det(A)) < 1e-12:
            bad.append('decal %d: singular map' % i)
        if len(bad) > 50:
            bad.append('...')
            break
    return x, bad


# ---------------------------------------------------------------------------------------------------- build

def next_build(builds, name):
    """<name>-<n>: one past every number a build, a partial one or a log has had (a build set aside to the trash keeps
    its log here, so its number is not reused)"""
    import re
    # (an empty log is this run's own stdout, redirected before it started)
    taken = [int(m.group(1)) for f in builds.glob(name + '-*')
             for m in [re.fullmatch(re.escape(name) + r'-(\d+)(\.partial|\.log)?', f.name)]
             if m and not (m.group(2) == '.log' and f.stat().st_size == 0)]
    return builds / ('%s-%d' % (name, max(taken, default=0) + 1))


def build(work, root, png, embedded, builds, mapname, origin, area, centre=None, radius=None, workers=8, sectors='*'):
    t0 = time.time()
    if area:
        cp2gltf.use_area(area)
        centre = centre or cp2gltf.AREAS[area]['centre']
        radius = radius or cp2gltf.AREAS[area]['radius']
    final = next_build(builds, mapname + '_decals')
    stage = final.with_name(final.name + '.partial')
    stage.mkdir(parents=True)
    lib = Library(root / 'json')
    tex = DecalTextures(png, embedded)
    pk3 = stage / ('zz_%s_decals.pk3' % mapname)
    z = zipfile.ZipFile(pk3, 'w', zipfile.ZIP_STORED, allowZip64=False)     # (no zip64: the engine cannot open one)
    conv = Converter(lib, tex, z, stage / 'side', workers)

    nodes, stats, nsec = read_nodes(work, workers, sectors)
    print('cp77decals: %d visible decal records in %d sectors (%.0f s)' % (len(nodes), nsec, time.time() - t0),
          flush=True)
    dropped = collections.Counter({'hidden in game (isVisibleInGame 0)': stats['hidden in game']})
    decals, notes = [], collections.Counter()
    for d in nodes:
        p = d['pos']
        if centre is not None and not cp2gltf.in_area(p[0], p[1], centre, radius, p[2]):
            dropped['outside the area'] += 1
            continue
        if min(abs(s) for s in d['scale']) < 1e-4:
            dropped['degenerate box (a scale under 0.1 mm)'] += 1
            continue
        if not d['mi'] or d['mi'].isdigit():
            # (a material path stored as a hash: 0, none)
            dropped['no material (path 0 or a hash)'] += 1
            continue
        rv = receivers(d)
        if not rv:
            dropped['render mode %s / %s' % (d['mode'], d['surface'])] += 1
            continue
        if min(d['cs']) < 0:
            # (blood_splatter_b has a node at -1 1 1: no colour goes below black)
            notes['colour scale below 0 (clamped to 0)'] += 1
            d['cs'] = tuple(max(c, 0.0) for c in d['cs'])
        m = conv.material(d['mi'], d['cs'])
        if isinstance(m, str):
            dropped['material: ' + m.split(':')[0]] += 1
            continue
        flags = (DF_FLIP_U if d['flip_u'] else 0) | (DF_FLIP_V if d['flip_v'] else 0) | \
            (DF_THRESHOLD if d['thr_on'] else 0) | (DF_REORIENT if d['reorient'] else 0) | rv
        if any(s < 0 for s in d['scale']):
            notes['negative scale (mirrored box)'] += 1
        if d['stretch']:
            notes['isStretchingEnabled set (not carried)'] += 1
        hide = cp2gltf.draw_distance({'hide': d['hide'], 'stream': d['stream']})
        M = box_map(d, origin)
        rec = {'map': [float(v) for v in M.ravel()], 'mat': m, 'alpha': float(np.clip(d['alpha'], 0.0, 1.0)),
               'hide': float(hide * UPM), 'flags': flags, 'thr': float(d['thr']), 'order': d['order'],
               'rough_scale': d['rough_scale'], 'cs_alpha': float(d['cs_alpha'])}
        if not all(math.isfinite(v) for v in rec['map'] + [rec['alpha'], rec['hide'], rec['thr']]):
            dropped['non-finite'] += 1
            continue
        rec['_src'] = d
        decals.append(rec)
    conv.close()
    print('cp77decals: %d materials (%d instances failed), %d images (%.0f s)' % (
        len(conv.materials), len(conv.failed), len(conv.images), time.time() - t0), flush=True)

    # material indices in (template, material) order; records by orderNo then material
    order = sorted(range(len(conv.materials)), key=lambda i: (conv.meta[i]['template'], conv.meta[i]['mi'].lower(),
                                                              conv.meta[i]['colour_scale']))
    remap = {old: new for new, old in enumerate(order)}
    materials = [conv.materials[i] for i in order]
    meta = [conv.meta[i] for i in order]
    for r in decals:
        r['mat'] = remap[r['mat']]
    decals.sort(key=lambda r: (r['order'], r['mat']))
    images = sorted(conv.images, key=conv.images.get)

    tex_bytes, side_bytes = conv.bytes['pack'], conv.bytes['side']
    blob = xdcl_blob(images, materials, decals)
    (stage / 'maps').mkdir(exist_ok=True)
    (stage / 'maps' / ('%s.xdcl' % mapname)).write_bytes(blob)       # (a loose copy for the renderer's tests)
    z.writestr(zipfile.ZipInfo('maps/%s.xdcl' % mapname), blob, compress_type=zipfile.ZIP_STORED)
    z.close()

    # check what was written: the pack's file read back
    with zipfile.ZipFile(pk3) as z:
        names = set(z.namelist())
        back = z.read('maps/%s.xdcl' % mapname)
    parsed, bad = validate(back, names, {'images': len(images), 'materials': len(materials), 'decals': len(decals)})

    by_template = collections.Counter(m['template'] for m in meta)
    decals_by_template = collections.Counter(meta[r['mat']]['template'] for r in decals)
    summary = {
        'decals': len(decals), 'dropped': dict(dropped), 'notes': dict(notes), 'sectors': nsec,
        'materials': len(materials), 'material_instances': len({m['mi'].lower() for m in meta}),
        'materials_by_template': dict(by_template), 'decals_by_template': dict(decals_by_template),
        'failed_instances': conv.failed, 'template_notes': {k: sorted(set(v)) for k, v in conv.notes.items()},
        'images': len(images), 'side_images': conv.side_images, 'texture_bytes': tex_bytes, 'side_bytes': side_bytes,
        'xdcl_bytes': len(blob), 'pk3_bytes': pk3.stat().st_size, 'missing_textures': sorted(tex.missing),
        'validation': bad, 'origin': list(origin), 'area': area,
    }
    (stage / ('%s.xdcl.json' % mapname)).write_text(json.dumps({
        'summary': summary, 'images': images,
        'materials': [dict(meta[i], record=materials[i]) for i in range(len(materials))]}, indent=1), encoding='utf-8')

    # 20 records nearest Misty's, decoded, for the renderer's tests
    near = sorted(range(len(decals)), key=lambda i: math.hypot(decals[i]['_src']['pos'][0] - MISTY[0],
                                                               decals[i]['_src']['pos'][1] - MISTY[1]))[:20]
    sample = []
    for i in near:
        r, s = decals[i], decals[i]['_src']
        R = cp2gltf.quat_rot(s['quat'])
        m = materials[r['mat']]
        sample.append({
            'index': i, 'record': {k: v for k, v in r.items() if k != '_src'},
            'cp_position_m': s['pos'], 'cp_scale_m': s['scale'], 'sector': s['sector'],
            'game_centre': ((np.array(s['pos']) - origin) * UPM).round(3).tolist(),
            'box_z_world': R[:, 2].round(4).tolist(), 'box_x_world': R[:, 0].round(4).tolist(),
            'box_y_world': (-R[:, 1]).round(4).tolist(),
            'material': dict({k: v for k, v in meta[r['mat']].items()}, record=m,
                             images={k: images[m[k]] for k in SLOTS if m[k] != NONE})})
    (stage / ('%s_decals_sample.json' % mapname)).write_text(json.dumps(sample, indent=1), encoding='utf-8')
    if bad:     # (a pack that fails its own check keeps the .partial folder and stops the run, not a numbered name)
        sys.exit('cp77decals: validation failed, the build stays in %s: %s' % (stage, '; '.join(bad[:10])))
    stage.rename(final)

    print('cp77decals: %s' % (final / pk3.name))
    print('  decals written %d; dropped %d: %s' % (len(decals), sum(dropped.values()),
                                                   ', '.join('%s %d' % kv for kv in dropped.most_common())))
    if notes:
        print('  notes: ' + ', '.join('%s %d' % kv for kv in notes.most_common()))
    print('  materials %d (%d instances, %d failed): %s' % (
        len(materials), summary['material_instances'], len(conv.failed),
        ', '.join('%s %d' % kv for kv in by_template.most_common())))
    print('  decals by template: ' + ', '.join('%s %d' % kv for kv in decals_by_template.most_common()))
    for mi, why in sorted(conv.failed.items()):
        print('  failed: %s: %s' % (mi, why))
    print('  images %d (%.1f MB DDS) + %d side images (%.1f MB); xdcl %.1f MB; pk3 %.1f MB; %d textures missing' % (
        len(images), tex_bytes / 1e6, len(conv.side_images), side_bytes / 1e6, len(blob) / 1e6,
        summary['pk3_bytes'] / 1e6, len(tex.missing)))
    print('  validation: %s (%.0f s)' % ('ok' if not bad else '; '.join(bad[:10]), time.time() - t0))
    return final, summary


def fetch_main(argv):
    """cp77decals.py needs|fetch <work> <root>"""
    ap = argparse.ArgumentParser(prog='cp77decals.py ' + argv[0],
                                 description='needs: write the depot paths the decal fetch at <root> still lacks; '
                                             'fetch: fetch them (wk_fetch.py) until nothing new is needed')
    ap.add_argument('work', type=Path, help='the area work folder (json/*.streamingsector.json)')
    ap.add_argument('root', type=Path, help='the decal fetch folder (wk_fetch.py layout: json/, png/, embedded/)')
    ap.add_argument('--area', default='', help="only the nodes in cp2gltf's area circle (default: every sector)")
    ap.add_argument('--out', type=Path, help='needs: the list to write (default <root>/needs.txt)')
    ap.add_argument('--retry', action='store_true', help='ask again for the paths in <root>/fetch_absent.txt')
    ap.add_argument('--workers', type=int, default=max(2, min(16, (os.cpu_count() or 4) - 2)))
    a = ap.parse_args(argv[1:])
    if argv[0] == 'fetch':
        n = fetch_loop(a.work, a.root, a.workers, a.area, a.retry)
    else:
        roots = fetch_roots(a.work, a.workers, a.area)
        n = needs(roots, a.root, a.retry)
        out = a.out or a.root / 'needs.txt'
        wk_fetch.write_list(n['need'], out)
        print('cp77decals needs: %d materials on the nodes; walked %s; %d needed -> %s; %d absent (fetch_absent.txt); '
              '%d embedded to export' % (len(roots), ', '.join('%s %d' % kv for kv in sorted(n['counts'].items())),
                                         len(n['need']), out, len(n['absent']), len(n['embedded'])))
    return 1 if n['need'] else 0


def main():
    if len(sys.argv) > 1 and sys.argv[1] in ('needs', 'fetch'):
        return fetch_main(sys.argv[1:])
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('work', type=Path, help='the area work folder (json/*.streamingsector.json)')
    ap.add_argument('root', type=Path, help='the decal fetch (json/: the serialised .mi, parents and templates)')
    ap.add_argument('builds', type=Path, help='the builds folder: writes <map>_decals-<n> in it')
    ap.add_argument('--map', default='cp_watson')
    ap.add_argument('--area', default='night_city_b', help="cp2gltf's area whose cells keep a decal ('' for none; night_city_b: the cp_watson map since tag 10)")
    ap.add_argument('--centre', type=float, nargs=2, help='CP metres; with --radius, overrides the area\'s circle')
    ap.add_argument('--radius', type=float)
    ap.add_argument('--origin', default='-1280 2240 12', help="the scene's origin in CP metres (Watson's hub frame)")
    ap.add_argument('--png', type=Path, help='the uncooked textures (default <root>/png)')
    ap.add_argument('--embedded', type=Path, help='the embedded textures (default <root>/embedded)')
    ap.add_argument('--workers', type=int, default=max(2, min(16, (os.cpu_count() or 4) - 2)))
    ap.add_argument('--sectors', default='*', help='a glob of sector names to read (tests)')
    ap.add_argument('--check', type=Path, help='only validate this .xdcl or pk3')
    a = ap.parse_args()
    if a.check:
        if a.check.suffix == '.pk3':
            with zipfile.ZipFile(a.check) as z:
                names = set(z.namelist())
                blob = z.read(next(n for n in names if n.endswith('.xdcl')))
        else:
            names, blob = None, a.check.read_bytes()
        x, bad = validate(blob, names)
        print('%d images, %d materials, %d decals: %s' % (len(x['images']), len(x['materials']), len(x['decals']),
                                                         'ok' if not bad else '; '.join(bad)))
        return 1 if bad else None
    build(a.work, a.root, a.png or a.root / 'png', a.embedded or a.root / 'embedded', a.builds, a.map,
          np.array([float(v) for v in a.origin.split()]), a.area, a.centre, a.radius, a.workers, a.sectors)


if __name__ == '__main__':
    sys.exit(main())
