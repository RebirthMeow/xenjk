"""w3scene.py -- the render scene (rd-vulkan .xscn, docs/levelport-scene.md) of a Witcher 3 work folder (Beauclair).
Plain Python. Reads w3gltf.py's placements, materials and meshes, w3terrain.py's tiles and w3build.py's
build record (the map's origin, so scene and BSP line up), and writes zz_<map>_scene.pk3:

    maps/<map>.xscn     version 6: every placed .w2mesh once with all its LODs (W3's LOD distances turned into
                        screen sizes), its materials, an instance per placement, the lights; the terrain as meshes
    scn/<map>/*.dds     the images, BC1/BC7 with mips (cp2gltf.SceneImages)
    materials.json      beside the pk3: what each material record came from

    python tools/levelport/w3scene.py --work DIR --build-json DIR/castle-2.build.json --map w3_castle --tag 1

Materials, first pass (`base` mode): one layer per material, the way the add-on wired the shader's inputs
(materials.json "nodes"): Witcher3_Main / glass / ivy take Diffuse, Normal and the roughness in the normal's alpha
(x RSpecScale + RSpecBase); pbr_std_wear_paint its BaseDiffuse / BaseNormal; pbr_vert_blend_colorize slice 0 of its
diffuse and normal arrays. The vertex-colour blends between layers need the renderer's second vertex stream (scene
v8): not yet. Normal maps are DirectX (green down) and are turned to OpenGL.

SpeedTree billboards (w3trees.py): a material record with a `card` (type 1 the vertical card, type 2 the
top card) gets a billboard record in maps/<map>.xw3b, version 3 then (the renderer turns the card and picks its view, scene.vert -9 / -10;
--bb-fade the pitch band of the top card's fade, 30 to 50 degrees).
SpeedTree wind (w3trees.py): a tree whose index entry carries `wind` gets its record in maps/<map>.xw3b version 4 (the
meshes made from it name it); its vertices' wind words are already in the stream (TEXCOORD_1, COLOR_0). --no-wind leaves
the section out.

Hide distances: the game hides a mesh past its autohide distance; <work>/autohide.json (w3autohide.py)
holds the depot's values, and each layer placement gets one as its instance hide distance: the component's or sector
block's forceAutoHideDistance where set, else the mesh's autoHideDistance, else 20 m (the class default: absent in 612
city meshes, never written as 20.0 by any of the 2251 that carry one, so read as the omitted default; not verified in
the engine), times --autohide-scale (1.5: MeshRenderingDistanceScale at every level-of-detail preset of the game's
graphics.xml; that it scales these distances is read from its name) plus the instance's radius (the renderer measures
from the sphere's centre; the game's measuring point is not known, so never earlier than either). Terrain foliage and
SpeedTree trees keep theirs; a proxy's members keep the proxy's near distance (mesh and shell must swap at one distance);
a proxy takes its own only past its near distance. --no-autohide: none (A/B).
"""
import argparse
import json
import re
import math
import os
import sys
import zipfile
from collections import Counter
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import gltfio  # noqa: E402
import vcache  # noqa: E402
import levelport  # noqa: E402
import pk3write  # noqa: E402
import w3paths  # noqa: E402
from cp2gltf import (SCENE_KIND, SCENE_VERT, SceneImages, snorm8, vertex_frame, xscn_blob, xscn_head_of,  # noqa: E402
                     xscn_rebuild)

TAN_HALF_VFOV = 0.75        # JKA's 90 degree horizontal view at 4:3
SKIP_TREES = ('Invisible', 'transparent_reflective')
# helpers the game never draws, wired by the add-on as ordinary main materials (shadow-blocking
# shells drawn as grey blocks and fighting the walls they sit in, debug bars up to 145 m long, checkerboard collision
# boxes, a fake sky plane). (The river and waterfall meshes stay, opaque, until water is converted: skipped, scene-5's
# lakes and river were dry beds)
SKIP_BASES = {'shadowmesh.w2mg', 'debug.w2mg', 'gridmat.w2mg', 'fake_sky_mtl.w2mg'}
# water (the renderer's water mode, Materials.water): the water meshes' materials (fountain basins, the stream under the
# palace bridge, cave pools, buckets; the puddles stay off on a dry day), and the surfaces w3water.py finds
WATER_MESH_MATERIALS = {'wyzima_castle_fountain_Material2', 'bob_cave_water_Material0', 'cave_water_puddle_Material0',
                        'bucket_water_Material0'}
WATER_MESH_BASES = set()     # (m_river_vertex_offset: a flowing river, W3_FLOW)
# falling water (Materials.flow): graph base name -> its texture (uncook path), scroll speed (tiles a second), tiling,
# opacity; per material instance overrides in W3_FLOW_MATERIALS (the cooked instances' values). The textures are read
# from the add-on's uncook folder: --w3 <research>/uncook, else $W3_UNCOOK or $W3_RESEARCH/uncook (w3paths.py)
W3_RESEARCH = None
W3_FLOW = {'waterfall_strips.w2mg': {'tex': r'dlc\bob\data\fx\textures\water\waterfall_strip_1.dds', 'speed': (0.0, -0.5),
                                     'tile': (1.0, 1.25), 'alpha': 0.75},
           'm_waterfall_mesh.w2mg': {'tex': r'fx\textures\water\waterfall_3.dds', 'speed': (0.07, -1.0),
                                     'tile': (1.91, 1.5), 'alpha': 0.75, 'rgb': 'brightness'},
           'm_waterfall_mesh_2.w2mg': {'tex': r'fx\textures\water\waterfall_1.dds', 'speed': (0.0, -0.75),
                                       'tile': (1.0, 1.0), 'alpha': 0.6}}
W3_FLOW['m_river_vertex_offset.w2mg'] = {
    # the rivers between the falls (bob_riverN meshes, sloped): the game's river graph scrolls wave_med_n along +U in two
    # layers ((0.6, 0) at (1, 1), (1.0, 0) at (3, 5)) over colour_shallow (30, 60, 50); in the water mode (bed through clear
    # water, ripples on world XY) these shallow streams read as dry beds
    'tex': r'environment\water\global_ocean\wave_med_n.dds', 'speed': (0.6, 0.0), 'tile': (3.0, 5.0), 'alpha': 0.7,
    'colour': (30, 60, 50)}
W3_FLOW['m_fountain_cascade.w2mg'] = {
    # the fountains' falling sheets (fountain_splash.w2mesh): waterfall_1 (a normal map, its alpha the sheet) at the
    # cooked instance's texture_coord (3, 0.25) scrolled by its texture_speed (0, -0.2); its alpha ramp thins the sheet
    'tex': r'fx\textures\water\waterfall_1.dds', 'speed': (0.0, -0.2), 'tile': (3.0, 0.25), 'alpha': 0.6}
W3_FLOW_MATERIALS = {'waterfall_strips_3_3_Material0': {'tile': (1.0, 1.5)}}
W3_WATER_TINT = (24, 52, 50)        # sRGB: the colour in the water (Toussaint's green rivers; no source value read)
# maps/<map>.xwat (cp2gltf WATER_XWAT_FIELDS): visibility, shore, ripple tile, speed, heading, roughness (0: the ORM's),
# normal strength, foam band, foam intensity
W3_WATER_XWAT = (1.0, 0.1, 4.0, 0.15, 0.0, 0.0, 0.7, 0.0, 0.0)   # (visibility 2 m, shore 0.5 m: shallow water vanished)
# meshes a building's proxy shell never stands for, kept whatever box they sit in (w3scene's proxy pass)
PROXY_KEEP_RE = re.compile(r'pavement|street|road|ground|stair|step|bridge|rock|cliff|terrain|floor|square|plaza', re.I)
ORM_MAX = 1024              # the ORM images' cap (--tex-size 2048 applies to colour and normals)


# ---------------------------------------------------------------------------------------------------- images

class Tex:
    """Source images (the add-on's uncooked DDS / TGA), read once, sized to at most --tex-size."""

    def __init__(self, size):
        self.size, self.cache, self.missing = size, {}, Counter()

    def get(self, path):
        if not path:
            return None
        if path in self.cache:
            return self.cache[path]
        from PIL import Image
        try:
            im = Image.open(path)
            im.load()
            im = im.convert('RGBA')
            w, h = im.size
            s = max(w, h)
            if s > self.size:
                f = self.size / s
                im = im.resize((max(1, int(w * f)), max(1, int(h * f))), Image.Resampling.LANCZOS)
            arr = np.asarray(im)
        except Exception:  # noqa: BLE001 - a file the reader cannot decode stays out (counted)
            self.missing[path] += 1
            arr = None
        self.cache[path] = arr
        return arr


GRASS_HIDE = 60.0   # (m: terrain foliage's grass and flowers, drawn to here)
GRASS_RE = re.compile(r'[\\/](grass|grass_w3|grass_common\w*)[\\/]', re.I)
WIND_RECORD = 320   # floats a SpeedTree's wind record (w3trees.WIND_RECORD)
# the .xw3b's XEXT section (after the cards and the wind): per material EXT_FLOATS floats ([0] 1: present) and EXT_PATHS
# 64 B image paths. Layer blend: 1 DiffuseOverride, 2-4 DiffuseOverrideColor (raw), 5 MaskSharpness, 6 UseOldMask,
# 7 LowMask, 8 HighMask, 9 BottomMaskRange, 10 TopMaskRange, 11 BlendingShift, 12 MaskTile, 13 Mask_Black_Point,
# 14 Mask_White_Point, 15 MaskFill, 16 ScalarRoughness, 17 RoughnessOverride; path 0 the Mask. Wear-paint: 1 GlobalWear,
# 2 GlobalWearBlur, 3 GlobalWearMax, 4 the wear masks' roughness share, 5 InvertMaskForGlobalWear,
# 6 UseRoughnessForPaintMask; path 0 the base roughness (the base normal's alpha, raw)
EXT_FLOATS = 24
EXT_PATHS = 2
BLOCKOUT = chr(92) + 'blockout_base' + chr(92)    # (lower case: the game's hidden placeholder meshes)


def src_image(v):
    """An input's source image and the channel used ('Color' or 'Alpha'), or (None, None)."""
    if isinstance(v, dict) and v.get('node') == 'TEX_IMAGE' and v.get('image'):
        return v['image'], v.get('out', 'Color')
    return None, None


def name64(path):
    """An image path as the 64 B NUL-padded field of the .xw3b and .xw3t records (refused when it does not fit with
    its NUL: every later record would be read shifted)"""
    b = path.encode('ascii')
    if len(b) >= 64:
        raise SystemExit('w3scene: image path %r is %d bytes; the record holds 63 (a shorter --map)' % (path, len(b)))
    return b.ljust(64, b'\0')


def src_value(v, default):
    if isinstance(v, dict):
        v = v.get('value', default)
    if v is None:
        return default
    return v


def array_slice(v, k=0):
    """A texture-array group input (DiffuseArray, NormalArray): the image of slice k."""
    if isinstance(v, dict) and v.get('node') == 'GROUP':
        ins = v.get('in') or {}
        img, _ = src_image(ins.get('Array_%d' % k))
        return img
    return None


def dx_to_gl_normal(t, keep_alpha=False):
    x = t[..., 0].astype(np.float32) / 127.5 - 1.0
    y = -(t[..., 1].astype(np.float32) / 127.5 - 1.0)
    z = np.sqrt(np.clip(1.0 - x * x - y * y, 0.0, 1.0))
    out = np.uint8(np.round(np.clip(np.stack([x, y, z], -1) * 127.5 + 127.5, 0, 255)))
    if keep_alpha and t.shape[-1] == 4:
        out = np.concatenate([out, t[..., 3:4]], -1)
    return out


def array_slices(v):
    """A texture-array group input's layer images, in order (Array_0, Array_1, ...)."""
    out = []
    if isinstance(v, dict) and v.get('node') == 'GROUP':
        ins = v.get('in') or {}
        k = 0
        while 'Array_%d' % k in ins:
            out.append(src_image(ins['Array_%d' % k])[0])
            k += 1
    return out


def combxyz(v, default=(0.0, 0.0)):
    """A COMBXYZ node's x, y (its inputs' values), or an unlinked vector's."""
    if isinstance(v, dict) and v.get('node') == 'COMBXYZ':
        ins = v.get('in') or {}
        return tuple(float(src_value(ins.get(k), d)) for k, d in (('X', default[0]), ('Y', default[1])))
    if isinstance(v, list) and len(v) >= 2:
        return float(v[0]), float(v[1])
    return default


def rgb(v, default):
    v = src_value(v, default)
    return [float(x) for x in (v if isinstance(v, (list, tuple)) else default)[:3]]


def raw_rgb(v, default):
    """A colour parameter as the game's shader receives it: the byte / 255 numbers, used there as linear multipliers
    (the game's shader cache: pbr_vert_blend_colorize's TextureColor, DiffuseOverrideColor, AO_Tint and pbr_std_wear_paint's
    tints reach the maths unconverted; only SpecularColor is raised to 2.2 in the shader). The Blender add-on hands them
    over sRGB-decoded (TextureColor 255 137 67 as 1.0 0.25 0.056), so this encodes them back. `default` is raw."""
    if not isinstance(src_value(v, None), (list, tuple)):
        return list(default)

    def enc(c):
        c = min(max(float(c), 0.0), 1.0)
        return c * 12.92 if c <= 0.0031308 else 1.055 * c ** (1 / 2.4) - 0.055
    return [enc(c) for c in rgb(v, default)]


def gl_normal_height(t):
    """A layer-blend normal array slice for the renderer: x, y as OpenGL normals (green flipped), the game's blue (the
    layer's height: pbr_vert_blend_colorize blends its layers by it, not a normal component; uncorrelated with the
    normal's z) kept in blue, the roughness alpha kept."""
    x = t[..., 0].astype(np.float32)
    y = 255.0 - t[..., 1].astype(np.float32)
    b = t[..., 2].astype(np.float32) if t.shape[-1] >= 3 else np.full(x.shape, 255.0, np.float32)
    a = t[..., 3].astype(np.float32) if t.shape[-1] == 4 else np.full(x.shape, 255.0, np.float32)
    return np.uint8(np.round(np.clip(np.stack([x, y, b, a], -1), 0, 255)))


# ---------------------------------------------------------------------------------------------------- materials

class Materials:
    def __init__(self, mats, images, tex, stats, layers=True):
        self.src, self.images, self.tex, self.stats = mats, images, tex, stats
        self.layers = layers
        self.recs, self.names, self.index = [], [], {}
        self.default_n = None
        self.blend = {}     # material index -> its layer-blend record (maps/<map>.xw3b)
        self.f0 = {}        # material index -> its specular colour F0 (linear rgb; the game's SpecularColor)
        self.kind = {}      # material index -> its .xw3b record's kind (float 23): 0 layer blend or F0 alone, 1 wear-paint,
                            # 2 a slope layer (snow.w2mg)
        self.card = {}      # material index -> its SpeedTree billboard card (w3trees.py: type 1 vertical, 2 top; .xw3b v3)
        self.ext = {}       # material index -> (EXT_FLOATS floats, EXT_PATHS paths): the .xw3b's XEXT section (the game's
                            # exact layer-blend and wear-paint terms, read from its shader cache)

    def slope_layer(self, ins):
        """environment/shaders/special/snow.w2mg (a Witcher3_Main material with SnowDiffuse wired; the castle's rocks
        with grass on top): its second layer, laid where the surface faces up, as maps/<map>.xw3b holds it (record kind
        2; layers 0): 20 floats (0, ThresholdMin, ThresholdMax, SnowTile u, v), 18 paths (0 SnowDiffuse, 1 SnowNormal).
        The Blender tools drive it by a colour ramp on the albedo and leave the thresholds unused; here they are read as
        the world normal's up component (0.6 to 1.0 on the rocks: grass on the tops, rock on the sides), a reading of
        their names, checked by look."""
        dif_p, _ = src_image(ins.get('SnowDiffuse'))
        nrm_p, _ = src_image(ins.get('SnowNormal'))
        d = self.tex.get(dif_p)
        if d is None:
            return None
        paths = [''] * 18
        paths[0] = self.images.add(np.ascontiguousarray(d[..., :3]), 'a')
        n = self.tex.get(nrm_p)
        if n is not None:
            paths[1] = self.images.add(dx_to_gl_normal(n, keep_alpha=True), 'n', alpha=n.shape[-1] == 4)
        tile = combxyz(ins.get('SnowTile'), (1.0, 1.0))
        v = [0.0, float(src_value(ins.get('ThresholdMin'), 0.6)), float(src_value(ins.get('ThresholdMax'), 1.0)),
             tile[0], tile[1]] + [0.0] * 15
        self.stats['slope_layers'] += 1
        return v, paths

    def wear_paint(self, ins, raw_base):
        """pbr_std_wear_paint's paint, wear and vertex AO as maps/<map>.xw3b holds them (record kind 1; layers 0, so a
        renderer without it reads the record as F0 alone): 20 floats, 18 image paths (0 the untinted base, which the wear
        shows, when the albedo has the tint baked in; 1 the blending masks, 2 the paint pattern, slice 0 of its array).
        The Blender tools' chain (witcher3_materials.blend, group pbr_std_wear_paint, "W3 Wear" nodes): paint by the
        vertex colour's blue past the masks' green, wear by one minus its alpha, AO by its red."""
        def f(k, d):
            return float(src_value(ins.get(k), d))
        mask_p, _ = src_image(ins.get('BlendingMasks'))
        pat_p = array_slice(ins.get('PatternArray'))
        paths = [''] * 18
        if raw_base is not None:
            paths[0] = self.images.add(np.ascontiguousarray(raw_base[..., :3]), 'a')
        m = self.tex.get(mask_p)
        if m is not None:
            paths[1] = self.images.add(np.ascontiguousarray(m[..., :3]), 'o')
        t = self.tex.get(pat_p)
        if t is not None:
            # (RGBA: the game's paint mask is the pattern's alpha times the vertex blue)
            paths[2] = self.images.add(np.ascontiguousarray(t if t.shape[-1] == 4 else t[..., :3]), 'a',
                                       alpha=t.shape[-1] == 4)
        wt = raw_rgb(ins.get('WearTint'), [1.0, 1.0, 1.0])
        wt8 = [int(round(min(max(c, 0.0), 1.0) * 255)) for c in wt]
        v = [0.0, f('PaintOpacity', 0.0), f('Paint_0', 0.0), f('Paint_1', 1.0), f('PaintBlendSharpness', 0.0),
             f('InvertPaintMask', 0.0)] + raw_rgb(ins.get('PaintTint'), [1.0, 1.0, 1.0]) + [f('PaintTiling', 1.0)]
        v += [f('WearOpacity', 0.0), f('Wear_0', 0.0), f('Wear_1', 1.0), f('WearBlendSharpness', 0.0),
              f('InvertWearMask', 0.0), f('WearBrightness', 1.0), f('WearSaturation', 1.0), f('UseAO', 0.0),
              f('BlendingMasksTiling', 1.0), float(wt8[0] | (wt8[1] << 8) | (wt8[2] << 16))]  # (the wear tint, rgb8)
        # XEXT: the game's global wear (strips paint, adds wear over the whole surface) and its masks' roughness
        # selection (shadercache/FORMULAS.md); the raw base roughness (the base normal's alpha) as its image
        nrm_p, _ = src_image(ins.get('BaseNormal'))
        n = self.tex.get(nrm_p)
        ext_paths = [''] * EXT_PATHS
        if n is not None and n.shape[-1] == 4:
            ext_paths[0] = self.images.add(np.ascontiguousarray(np.repeat(n[..., 3:4], 3, -1)), 'o')
        e = [1.0, f('GlobalWear', 0.0), f('GlobalWearBlur', 0.0), f('GlobalWearMax', 1.0),
             max(f('UseRoughnessForWearMask', 0.0), f('UseRoughnessForGlobalWear', 0.0)),
             f('InvertMaskForGlobalWear', 0.0), f('UseRoughnessForPaintMask', 0.0)]
        self.ext[self._wear_index] = (e + [0.0] * (EXT_FLOATS - len(e)), ext_paths)
        self.stats['wear_paint_layers'] += 1
        self.stats['wear_paint_global_wear'] += 1 if f('GlobalWear', 0.0) > 0.0 else 0
        return v, paths

    def layer_blend(self, ins):
        """pbr_vert_blend_colorize's layers and values as maps/<map>.xw3b holds them: (20 floats, 18 image paths)"""
        cols, nrms = array_slices(ins.get('DiffuseArray'))[:8], array_slices(ins.get('NormalArray'))[:8]
        n = len(cols)
        if not n:
            return None
        paths = []
        for k in range(8):
            t = self.tex.get(cols[k]) if k < n else None
            paths.append(self.images.add(np.ascontiguousarray(t[..., :3]), 'a') if t is not None else '')
        for k in range(8):
            t = self.tex.get(nrms[k]) if k < n and k < len(nrms) else None
            # (x, y, the game's height in blue, roughness: the renderer rebuilds the normal from x and y)
            paths.append(self.images.add(gl_normal_height(t), 'n', alpha=True) if t is not None else '')
        pat_p, _ = src_image(ins.get('PatternTexture'))
        mask_p, _ = src_image(ins.get('PatternMask'))
        pat = self.tex.get(pat_p)
        mask = self.tex.get(mask_p)
        paths.append(self.images.add(np.ascontiguousarray(pat), 'a', alpha=True) if pat is not None else '')
        paths.append(self.images.add(np.ascontiguousarray(mask[..., :3]), 'o') if mask is not None else '')
        off = combxyz(ins.get('PatternUVOffset'))

        def f(k, d):
            return float(src_value(ins.get(k), d))
        p = [float(n), f('ColoredTextureBottom', 0.0), f('ColoredTextureTop', -1.0), f('AOPower', 0.0)]
        p += raw_rgb(ins.get('AO_Tint'), [1.0, 1.0, 1.0]) + [f('RSpecScale', 1.0)]
        p += raw_rgb(ins.get('TextureColor'), [1.0, 1.0, 1.0]) + [f('RSpecBase', 0.0)]
        p += [f('PatternBottomLayer', 0.0), f('PatternTopLayer', -1.0), f('UsePattern', 0.0) if pat is not None else 0.0,
              f('PatternUVScale', 1.0)]
        p += [off[0], off[1], 0.0, 0.0]
        # XEXT: the game's exact blend (shadercache/FORMULAS.md): the diffuse override, the layer mask (the upper
        # layer's height or the Mask noise, sharpened), the blending shift, the pattern mask's levels and fill
        m_p, _ = src_image(ins.get('Mask'))
        mt = self.tex.get(m_p)
        ext_paths = [''] * EXT_PATHS
        if mt is not None:
            ext_paths[0] = self.images.add(np.ascontiguousarray(mt[..., :3] if mt.ndim == 3 else np.repeat(mt[..., None], 3, -1)), 'o')
        tile = combxyz(ins.get('MaskTile'), (1.0, 1.0))[0]
        e =[1.0, f('DiffuseOverride', 0.0)] + raw_rgb(ins.get('DiffuseOverrideColor'), [1.0, 1.0, 1.0])
        e += [f('MaskSharpness', 1.0), f('UseOldMask', 0.0), f('LowMask', 0.0), f('HighMask', 1.0),
              f('BottomMaskRange', 0.0), f('TopMaskRange', 1.0), f('BlendingShift', 0.0), tile,
              f('Mask_Black_Point', 0.0), f('Mask_White_Point', 1.0), f('MaskFill', 0.0),
              f('ScalarRoughness', 0.5), f('RoughnessOverride', 0.0)]
        self._blend_ext = (e + [0.0] * (EXT_FLOATS - len(e)), ext_paths)
        self.stats['blend_layers_%d' % n] += 1
        self.stats['blend_diffuse_override'] += 1 if f('DiffuseOverride', 0.0) > 0.0 else 0
        return p, paths

    def main_group(self, rec):
        for nd in (rec.get('nodes') or {}).values():
            t = nd['tree'].split('.')[0]
            if not t.startswith('WitcherTexArray'):
                return t, nd['inputs']
        return None, {}

    def record(self, name):
        """The scene material index of a Blender material name, or None (skipped)."""
        if name in self.index:
            return self.index[name]
        rec = self.src.get(name)
        tree, ins = self.main_group(rec or {})
        base = os.path.basename((rec or {}).get('base') or '').lower()
        if base in W3_FLOW:
            self.index[name] = self.flow(name, base)
            return self.index[name]
        if name in WATER_MESH_MATERIALS or base in WATER_MESH_BASES:
            self.stats['materials_water_' + name] += 1
            self.index[name] = self.water()
            return self.index[name]
        if base in SKIP_BASES:
            self.stats['materials_skipped_' + base] += 1
            self.index[name] = None
            return None
        if rec is None or tree is None or tree in SKIP_TREES or name.startswith('Witcher Particle'):
            self.stats['materials_skipped_' + (tree or 'none')] += 1
            self.index[name] = None
            return None
        kind = 'opaque'
        if tree == 'pbr_vert_blend_colorize':
            alb_p, nrm_p, alpha_p = array_slice(ins.get('DiffuseArray')), array_slice(ins.get('NormalArray')), None
            rough_from = None
        elif tree == 'pbr_std_wear_paint':
            alb_p, _ = src_image(ins.get('BaseDiffuse') or ins.get('Diffuse'))
            nrm_p, _ = src_image(ins.get('BaseNormal') or ins.get('Normal'))
            # roughness in the base normal's alpha, as the main shader's (the Blender tools leave it unwired, so every wear-paint surface read one constant)
            alpha_p, rough_from = None, ((nrm_p, 'Alpha') if nrm_p else src_image(ins.get('Roughness')))
        else:
            alb_p, _ = src_image(ins.get('Diffuse'))
            nrm_p, _ = src_image(ins.get('Normal'))
            alpha_p, _ = src_image(ins.get('Alpha'))
            rough_from = src_image(ins.get('Roughness'))
            if tree == 'Witcher3_Glass' or 'glass.w2mi' in (rec.get('base') or '').lower():
                # (the palace's window glass, *_glass.w2mi, is a glass instance the Blender tools wire as
                # Witcher3_Main; as a cutout its panes were cut away and the towers' tall windows showed the sky through
                # them: the two "cyan shafts")
                kind = 'blend'
            elif tree == 'pbr_hair_moving' or (alpha_p and rec.get('blend_method') not in (None, 'OPAQUE')):
                kind = 'cutout'
        if kind == 'cutout':
            # the add-on marks every Witcher3_Main material with the Diffuse alpha linked as a hashed blend; one whose
            # albedo is opaque throughout is opaque (91 of 101)
            t = self.tex.get(alb_p)
            if t is not None and t.shape[-1] == 4 and int(t[..., 3].min()) >= 250:
                kind = 'opaque'
                self.stats['cutouts_opaque_albedo'] += 1
        rs_scale = float(src_value(ins.get('RSpecScale'), 1.0))
        rs_base = float(src_value(ins.get('RSpecBase'), 0.0))
        alb = self.tex.get(alb_p)
        nrm = self.tex.get(nrm_p)
        raw_base = None     # (wear-paint: the base before its tint, which the wear shows)
        if tree == 'pbr_std_wear_paint' and alb is not None:
            # the base as the game draws it before paint and wear (its shader cache, shadercache/FORMULAS.md):
            # lerp(BaseDiffuse, BaseDiffuse x BaseDiffuseTint, BaseDiffuseTintScale), the tint raw (byte / 255). The
            # Blender tools' "W3 Wear Tinted Base" multiplied by the decoded tint and the scale: a roof tinted (255, 25,
            # 30) at 0.2 drew at a fifth of its brightness, deep red, where the game's keeps 82-100 %
            tint = raw_rgb(ins.get('BaseDiffuseTint'), [1.0, 1.0, 1.0])
            scale = float(src_value(ins.get('BaseDiffuseTintScale'), 1.0))
            if tint != [1.0, 1.0, 1.0] and scale != 0.0:
                raw_base = alb
                lin = (alb[..., :3].astype(np.float32) / 255.0) ** 2.2
                lin = lin * (1.0 + scale * (np.array(tint, np.float32) - 1.0))
                alb = np.concatenate([np.uint8(np.round(np.clip(lin, 0, 1) ** (1 / 2.2) * 255)), alb[..., 3:]], -1)
                self.stats['wear_paint_base_tinted'] += 1
        var = ins.get('VarianceColor')
        if tree == 'Witcher3_Main' and isinstance(var, dict) and alb is not None and rec.get('cloth'):
            # VarianceColor: neutral at sRGB 128 (linear 0.2159) on all but one castle material; the wedding garland's
            # red_cloth is white_cloth at (128, 38, 35): read as a tint about 128 (sRGB / 0.5), baked when not
            # neutral (the game's formula, with VarianceOffset, unknown). Cloth only: 30 other
            # castle materials carry greys 36-240 and browns at offsets 0.25-10, which as a multiply darkened the tower
            # windows to 0.28 (scene-12): their meaning is not this
            vc = [min(max(c, 0.0), 1.0) ** (1 / 2.2) / (128 / 255) for c in rgb(var, [0.2159] * 3)]
            if max(abs(c - 1.0) for c in vc) > 0.03:
                t = np.clip(alb[..., :3].astype(np.float32) * np.array(vc, np.float32), 0, 255)
                alb = np.concatenate([np.uint8(np.round(t)), alb[..., 3:]], -1)
                self.stats['variance_tinted'] += 1
        paths = ['', '', '']
        if alb is not None:
            a = alb if kind in ('cutout', 'blend') else alb[..., :3]
            paths[0] = self.images.add(np.ascontiguousarray(a), 'a', alpha=kind != 'opaque',
                                       coverage=kind == 'cutout')
        else:
            gv = src_value(ins.get('Diffuse'), [0.5, 0.5, 0.5]) if not isinstance(ins.get('Diffuse'), dict) else 0.5
            grey = float(gv[0] if isinstance(gv, (list, tuple)) else gv)
            paths[0] = self.images.add(np.full((4, 4, 3), int(round(255 * grey ** (1 / 2.2))), np.uint8), 'a')
            self.stats['materials_no_albedo'] += 1
        if nrm is not None:
            paths[1] = self.images.add(dx_to_gl_normal(nrm[..., :3]), 'n')
        # ORM: occlusion 1, roughness from the normal's alpha (or a flat value), metal 0
        rough = None
        if rough_from and rough_from[0]:
            rt = self.tex.get(rough_from[0])
            if rt is not None:
                ch = 3 if rough_from[1] == 'Alpha' else 0
                rough = np.clip(rt[..., ch].astype(np.float32) / 255.0 * rs_scale + rs_base, 0.02, 1.0)
        if rough is None:
            rough = np.full((4, 4), float(np.clip(src_value(ins.get('ScalarRoughness'), 0.6) * rs_scale + rs_base,
                                                 0.02, 1.0)), np.float32)
        orm = np.stack([np.full(rough.shape, 255, np.uint8), np.uint8(np.round(rough * 255)),
                        np.zeros(rough.shape, np.uint8)], -1)
        if max(orm.shape[:2]) > ORM_MAX:     # roughness alone: kept at 1024 when colour and normals go higher
            from PIL import Image
            f = ORM_MAX / max(orm.shape[:2])
            orm = np.asarray(Image.fromarray(orm).resize((max(1, int(orm.shape[1] * f)), max(1, int(orm.shape[0] * f))),
                                                         Image.Resampling.BOX))
        paths[2] = self.images.add(orm, 'o')
        i = len(self.recs)
        # (the flags' cloth: opaque and double-sided, w3flags.py's 'cloth' block)
        flags = 1 if kind in ('cutout', 'blend') or (rec.get('cloth') or {}).get('double_sided') else 0
        if kind == 'cutout' and name.startswith('srt_'):
            # SpeedTree leaf cards and fronds (w3trees.py): the renderer's canopy shading (flag 0x800, r_sceneFoliage:
            # the rounded canopy normal, wrap, light through leaves); never set, trees were lit card by card
            flags |= 0x800
            self.stats['materials_leaf'] += 1
        self.recs.append((paths, SCENE_KIND[kind], flags, [1.0, 1.0, 1.0, 0.0], [1.0, 1.0, 1.0, 1.0], [0.0, 0.0]))
        self.names.append({'index': i, 'name': name, 'tree': tree, 'kind': kind, 'base': rec.get('base'),
                           'albedo': alb_p, 'normal': nrm_p, 'images': paths})
        if tree == 'pbr_vert_blend_colorize' and self.layers:
            got = self.layer_blend(ins)
            if got:
                self.blend[i] = got
                self.ext[i] = self._blend_ext
                self.names[-1]['layer_blend'] = {'values': got[0], 'images': got[1], 'ext': self._blend_ext}
        if tree == 'Witcher3_Main' and self.layers and 'snow.w2mg' in (rec.get('base') or '').lower() \
                and isinstance(ins.get('SnowDiffuse'), dict):
            got = self.slope_layer(ins)
            if got:
                self.blend[i] = got
                self.kind[i] = 2
                self.names[-1]['slope_layer'] = {'values': got[0], 'images': got[1]}
        if tree == 'pbr_std_wear_paint' and self.layers:
            self._wear_index = i
            got = self.wear_paint(ins, raw_base)
            self.blend[i] = got
            self.kind[i] = 1
            self.names[-1]['wear_paint'] = {'values': got[0], 'images': got[1]}
        self.stats['materials_' + tree] += 1
        # the specular colour (F0): the game's main shaders take it from SpecularColor (pbr_std.w2mg declares no other
        # specular input): plaster ~0.02, polished marble up to 0.43 gold-tinted
        # Only a wired value counts: an unwired input holds the Blender group's default (1.0: every proxy a mirror, the
        # first try's cyan towers). Only the main shaders: the layer-blend and wear-paint graphs' SpecularColor
        # (marble 0.43 0.28 0.03) made gold mirrors of the pillars, which the game's are not; its role there is unverified
        spec = ins.get('SpecularColor')
        if isinstance(spec, dict) and tree in ('Witcher3_Main', 'Witcher3_Glass', 'pbr_hair_moving'):
            f0 = rgb(spec, [0.04, 0.04, 0.04])
            if max(f0) > 0.0:
                self.f0[i] = [min(max(c, 0.0), 1.0) for c in f0]
                self.stats['materials_f0'] += 1
        if isinstance(rec.get('card'), dict) and kind == 'cutout':
            self.card[i] = rec['card']
            self.names[-1]['card'] = rec['card']
            self.stats['materials_billboard_card_%d' % rec['card'].get('type', 0)] += 1
        self.index[name] = i
        return i

    def flow(self, name, base):
        """Falling water (W3_FLOW: the waterfall strips' and the dams' graphs, which the add-on wires as plain grey main
        materials): a two-sided blend, its colour a white sheet with the game texture's alpha as opacity, the texture's
        own normal (waterfall_strip_1 and waterfall_1 are normal maps with an opacity mask), the .xw3b record kind 3 with
        the scroll speed (tiles a second), tiling and opacity the renderer scrolls them by (scene.frag -8). Values from
        the cooked material instances."""
        spec = dict(W3_FLOW[base], **W3_FLOW_MATERIALS.get(name, {}))
        t = self.tex.get(os.path.join(w3paths.uncook(W3_RESEARCH), spec['tex']))
        if t is None or t.shape[-1] < 4:
            self.stats['flow_missing_texture'] += 1
            return None
        a = t[..., 3]
        if spec.get('colour'):      # (a river: the water's colour, opaque by md.z alone; the texture its normal)
            a = np.full(t.shape[:2], 255, np.uint8)
            col = np.empty(t.shape[:2] + (3,), np.uint8)
            col[:] = spec['colour']
            nrm = dx_to_gl_normal(t[..., :3], keep_alpha=False)
        elif spec.get('rgb') == 'brightness':     # (waterfall_3: red the sheet's brightness, alpha its opacity)
            v = t[..., 0]
            col = np.stack([v * 0.9, v * 0.95, v], -1).astype(np.uint8)
            nrm = None
        else:
            col = np.empty(t.shape[:2] + (3,), np.uint8)
            col[:] = (226, 236, 240)
            nrm = dx_to_gl_normal(t[..., :3], keep_alpha=False)
        paths = [self.images.add(np.ascontiguousarray(np.dstack([col, a])), 'a', alpha=True),
                 self.images.add(nrm, 'n') if nrm is not None else '',
                 self.images.add(np.full((4, 4, 3), (255, round(255 * 0.08), 0), np.uint8), 'o')]
        i = len(self.recs)
        self.recs.append((paths, SCENE_KIND['blend'], 1, [1.0, 1.0, 1.0, 0.0], [1.0, 1.0, 1.0, 1.0], [0.0, 0.0]))
        self.names.append({'index': i, 'name': name, 'tree': 'flow', 'kind': 'blend', 'base': base, 'images': paths})
        sp, tl = spec['speed'], spec['tile']
        self.blend[i] = ([0.0, sp[0], sp[1], tl[0], tl[1], spec['alpha']] + [0.0] * 14, [''] * 18)
        self.kind[i] = 3
        self.stats['materials_flow'] += 1
        return i

    def water(self):
        """The renderer's water mode (Night City's contract, cp2gltf WATER_*): a blend flagged 0x80, no colour image, a
        ripple normal map, rough 0.05, tint the colour in the water. One material for every Witcher 3 water surface: the
        sea-level water, the lakes (w3water.py) and the water meshes (WATER_MESH_MATERIALS)."""
        if 'w3_water' in self.index:
            return self.index['w3_water']
        from cp2gltf import WATER_FLAG, water_ripple_normal
        paths = ['', self.images.add(water_ripple_normal(), 'n'),
                 self.images.add(np.full((4, 4, 3), (255, round(255 * 0.05), 0), np.uint8), 'o')]
        i = len(self.recs)
        self.recs.append((paths, SCENE_KIND['blend'], WATER_FLAG, [1.0, 1.0, 1.0, 0.0],
                          [c / 255.0 for c in W3_WATER_TINT] + [1.0], [0.0, 0.0]))
        self.names.append({'index': i, 'name': 'w3_water', 'tree': 'water', 'kind': 'water', 'images': paths})
        self.index['w3_water'] = i
        return i

    def flat(self, key, rgb, rough, flags=0):
        if key in self.index:
            return self.index[key]
        paths = [self.images.add(np.full((4, 4, 3), [int(round(255 * c ** (1 / 2.2))) for c in rgb], np.uint8), 'a'),
                 '', self.images.add(np.full((4, 4, 3), (255, int(round(255 * rough)), 0), np.uint8), 'o')]
        i = len(self.recs)
        self.recs.append((paths, SCENE_KIND['opaque'], flags, [1.0, 1.0, 1.0, 0.0], [1.0, 1.0, 1.0, 1.0], [0.0, 0.0]))
        self.names.append({'index': i, 'name': key, 'tree': 'flat', 'kind': 'opaque', 'images': paths})
        self.index[key] = i
        return i


# ---------------------------------------------------------------------------------------------------- scene

def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--work', required=True)
    ap.add_argument('--build-json', required=True, help="w3build.py's record (its box and origin)")
    ap.add_argument('--map', default='w3_castle')
    ap.add_argument('--tag', default='1')
    ap.add_argument('--out')
    ap.add_argument("--tex-size", type=int, default=2048)
    ap.add_argument('--light-scale', type=float, default=1.0, help='times the game brightness')
    ap.add_argument('--light-fog', type=float, default=0.3, help="each light's share in the haze")
    ap.add_argument('--terrain-step', type=int, default=1)
    ap.add_argument('--far-step', type=int, default=8, help='the far terrain ring: every Nth vertex')
    ap.add_argument('--version', type=int, default=5, choices=(5, 6), help='scene version (the installed renderer: 5)')
    ap.add_argument('--no-wind', action='store_true', help="no SpeedTree wind section in maps/<map>.xw3b (the trees "
                    "stay still whatever r_sceneTreeWind says)")
    ap.add_argument('--no-layers', action='store_true', help='no maps/<map>.xw3b (layer blends): first layers only')
    ap.add_argument('--no-water', action='store_true', help='no sea or lake surfaces (w3water.py)')
    ap.add_argument('--no-vcache', action='store_true', help='triangles in the source order (vcache.py off; A/B)')
    ap.add_argument('--no-autohide', action='store_true', help="no hide distances from the game's (autohide.json; A/B)")
    ap.add_argument('--autohide-scale', type=float, default=1.5, help="times the game's autohide distances")
    ap.add_argument('--card-match', help="cardmatch.py's match.json: each billboard card's match to its 3D tree")
    ap.add_argument('--bb-fade', type=float, nargs=2, default=(30.0, 50.0), metavar=('START', 'END'),
                    help="billboards: the view's pitch (degrees) where the top card starts / finishes replacing the "
                         "vertical card (.xw3b version 3)")
    ap.add_argument('--w3', help="a research folder whose uncook/ is the add-on's uncook folder (the falling water's "
                                 "textures; default $W3_UNCOOK, else $W3_RESEARCH/uncook)")
    a = ap.parse_args()
    for name, v in (('--terrain-step', a.terrain_step), ('--far-step', a.far_step)):
        if v < 1 or 256 % v:
            ap.error('%s %d: a terrain tile is 256 cells a side; the step must divide 256' % (name, v))
    global W3_RESEARCH
    W3_RESEARCH = a.w3

    work = Path(a.work)
    build = json.load(open(a.build_json, encoding='utf-8'))
    origin = np.array(build['origin'], dtype=np.float64)
    x0, y0, x1, y1 = build['box']
    upm = levelport.UNITS_PER_METRE
    pl = json.load(open(work / 'placements.json', encoding='utf-8'))
    index = json.load(open(work / 'meshes' / 'index.json', encoding='utf-8'))
    stats = Counter()
    images = SceneImages(a.map, work / 'scene_dds')
    mats = Materials(json.load(open(work / 'materials.json', encoding='utf-8')), images, Tex(a.tex_size), stats,
                     layers=not a.no_layers)

    verts, idx, stream = [], [], []
    count = {'v': 0, 'i': 0}
    meshes, lods, parts = [], [], []
    mesh_of = {}
    wind_of = {}        # mesh index -> its SpeedTree's depot path, where the tree carries wind (w3trees.py)
    mesh_near = {}      # mesh index -> where its first LOD starts (m): the game's entity proxies start at 50-200 m
    mesh_box = {}       # mesh index -> its LOD 0's local box (lo, hi), metres
    docs = {}

    def geometry(pos, nrm, uv, tris, uv1=None, col=None, mi=None):
        # triangles in vertex-cache order (vcache.py: the source order cost 1.43 cache misses a triangle, the
        # optimised 1.03); a blended material keeps its order (its triangles draw back to front
        # as authored)
        if not a.no_vcache and len(tris) > 1 and (mi is None or mats.recs[mi][1] != SCENE_KIND['blend']):
            tris = vcache.order(tris, len(pos)).astype(np.asarray(tris).dtype)
            stats['vcache_parts'] += 1
        n, t = vertex_frame(pos, nrm, uv, tris)
        # maps/<map>.xw3b's stream, in the same vertex order: the second UV set's bits, the colour's bytes
        st = np.zeros((len(pos), 3), np.uint32)
        if uv1 is not None and len(uv1) == len(pos):
            st[:, :2] = np.ascontiguousarray(uv1, dtype='<f4').view('<u4').reshape(-1, 2)
        if col is not None and len(col) == len(pos):
            c = np.clip(np.rint(np.asarray(col, dtype=np.float64) * 255.0), 0, 255).astype(np.uint32)
            st[:, 2] = c[:, 0] | (c[:, 1] << 8) | (c[:, 2] << 16) | (c[:, 3] << 24)
        stream.append(st)
        v = np.zeros(len(pos), SCENE_VERT)
        v['pos'] = pos
        v['nrm'][:, :3] = snorm8(n)
        v['tan'] = snorm8(t)
        if uv is not None:
            v['uv'] = uv
        verts.append(v)
        idx.append(tris.astype('<u4').ravel())
        g = (count['i'], tris.size, count['v'])
        count['v'] += len(pos)
        count['i'] += tris.size
        return g

    def scene_mesh(path, slots):
        key = (path, tuple(slots))
        if key in mesh_of:
            return mesh_of[key]
        rec = index.get(path)
        if rec is None:
            mesh_of[key] = None
            return None
        if path not in docs:
            docs[path] = gltfio.Document(work / 'meshes' / rec['file'])
        doc = docs[path]
        prims = doc.doc['meshes'][0]['primitives']
        data = doc.primitives(0)
        cols = doc.colors(0)
        by_lod = {}
        swept = []      # (a turning billboard card: the circle each vertex sweeps about the mesh's z axis, as a box)
        for pr, (pos, nrm, uv, tri, _m), col in zip(prims, data, cols):
            ex = pr.get('extras', {})
            if (ex.get('billboard') or {}).get('part') == 'card':
                rxy = float(np.sqrt((pos[:, :2] ** 2).sum(1)).max()) if len(pos) else 0.0
                swept.append((np.array([-rxy, -rxy, pos[:, 2].min()]), np.array([rxy, rxy, pos[:, 2].max()])))
            at = pr['attributes']
            uv1 = doc.accessor(at['TEXCOORD_1']).astype(np.float64) if 'TEXCOORD_1' in at else None
            if ex.get('lod', 0) < 0:      # (one city proxy carries a stray LOD -1 at 0 m: it drew everywhere)
                continue
            by_lod.setdefault(ex.get('lod', 0), []).append((ex.get('material', 0), ex.get('distance', 0.0),
                                                            pos, nrm, uv, tri, uv1, col))
        levels = sorted(by_lod)
        if not levels:
            mesh_of[key] = None
            return None
        lo, hi = np.full(3, np.inf), np.full(3, -np.inf)
        for _, _, pos, *_ in by_lod[levels[0]]:
            lo, hi = np.minimum(lo, pos.min(0)), np.maximum(hi, pos.max(0))
        box0 = (lo.copy(), hi.copy())
        for slo, shi in swept:      # (the sphere holds the card at every turn: no culling pop as it turns)
            lo, hi = np.minimum(lo, slo), np.maximum(hi, shi)
            stats['billboard_sphere_grown'] += int(np.any(slo < box0[0] - 1e-6) or np.any(shi > box0[1] + 1e-6))
        radius = float(np.linalg.norm(hi - lo)) / 2
        first_lod = len(lods)
        for lv in levels:
            first_part = len(parts)
            if len(lods) > first_lod:
                # LOD k draws until the next drawn level's distance: as a screen size, the sphere's diameter over the
                # view height (a level with no drawn part is passed over: the one before hands over at this one's)
                d = by_lod[lv][0][1]
                lods[-1][2] = (2 * radius) / (2 * d * TAN_HALF_VFOV) if d > 0 else 0.0
            for mid, _d, pos, nrm, uv, tri, uv1, col in by_lod[lv]:
                name = slots[mid] if mid < len(slots) else None
                mi = mats.record(name) if name else None
                if mi is None:
                    continue
                parts.append(geometry(pos, nrm, uv, tri, uv1, col, mi) + (mi,))
            if len(parts) == first_part:
                continue
            lods.append([first_part, len(parts) - first_part, 0.0])
        if len(lods) == first_lod:
            mesh_of[key] = None
            return None
        lods[-1][2] = 0.0
        meshes.append(((lo + hi) / 2, radius, first_lod, len(lods) - first_lod))
        mesh_box[len(meshes) - 1] = box0     # (LOD 0's: the proxies' and walls' tests)
        mesh_of[key] = len(meshes) - 1
        if rec.get('wind') and not a.no_wind:
            wind_of[len(meshes) - 1] = path      # (a SpeedTree's wind: maps/<map>.xw3b version 4)
        # a mesh whose first LOD starts away from the eye (the game's entity_proxy shells: one LOD from 50-200 m) is
        # drawn only past it, the instance's near distance (scene version 5); drawn everywhere, the castle's walls and
        # stairs showed the proxies' 2-33 px/m atlases
        mesh_near[mesh_of[key]] = float(by_lod[levels[0]][0][1] or 0.0)
        return mesh_of[key]

    # the game's hide distances (w3autohide.py): sector blocks by layer, mesh and position (10 cm, the neighbours too),
    # entity components by layer and mesh (the larger where a layer's copies differ: 11 of the city's groups)
    ah_mesh, ah_sec, ah_comp = {}, {}, {}
    if not a.no_autohide and (work / 'autohide.json').is_file():
        A = json.load(open(work / 'autohide.json', encoding='utf-8'))
        ah_mesh = A['mesh']
        for lay, d in A['sector'].items():
            for mp, rows in d.items():
                for x, y, z, fa, *_ in rows:
                    k = (lay, mp, round(x * 10), round(y * 10), round(z * 10))
                    ah_sec[k] = max(ah_sec.get(k, 0), fa)
        for lay, d in A['component'].items():
            for mp, rows in d.items():
                ah_comp[(lay, mp)] = max([ah_comp.get((lay, mp), 0)] + [r[0] for r in rows])

    def autohide(p, t):
        """The game's autohide distance of a layer placement (m), its source; (None, None) where it has none"""
        lay, mp = p.get('layer'), p['mesh'].lower()
        force = None
        if p.get('component') is None:      # (a sector block)
            k = (lay, mp, round(t[0] * 10), round(t[1] * 10), round(t[2] * 10))
            force = ah_sec.get(k)
            for d in ((dx, dy, dz) for dx in (-1, 0, 1) for dy in (-1, 0, 1) for dz in (-1, 0, 1)):
                if force is not None:
                    break
                force = ah_sec.get((lay, mp, k[2] + d[0], k[3] + d[1], k[4] + d[2]))
        else:
            force = ah_comp.get((lay, mp))
        if force:
            return float(force), 'force'
        if p['mesh'] not in ah_mesh:
            return None, None
        v = ah_mesh[p['mesh']]
        return (float(v), 'mesh') if v is not None else (20.0, 'default')

    inst = []
    inst_layer, inst_veg, inst_name = [], [], []
    seen_twins = set()
    dropped_meshes = Counter()      # (mesh, its slots' material bases) of placements that draw nothing
    mats_src = mats.src
    for p in pl['placements']:
        if p['kind'] != 'w2mesh':
            stats['placements_' + p['kind']] += 1
            continue
        if BLOCKOUT in p['mesh'].lower():
            # the game's blockout placeholders, hidden in its renderer (the add-on hides them too): the castle's four
            # capsule_stone.w2mesh drew as the "cyan shafts" by its towers
            stats['placements_blockout'] += 1
            continue
        M = np.array(p['matrix'], dtype=np.float64)
        t = M[:3, 3]
        if not (x0 - 50 <= t[0] <= x1 + 50 and y0 - 50 <= t[1] <= y1 + 50):
            stats['placements_outside'] += 1
            continue
        # exact twins: a sector's packed copy beside its layer component (4,377 pairs within 2 cm,
        # 396 of them glass blended twice, reading darker than their neighbours); the first is kept
        twin = (p['mesh'], json.dumps(p['slots']), tuple(np.round(t * 50).astype(int)), tuple(np.round(M[:3, :3] * 100).astype(int).ravel()))
        if twin in seen_twins:
            stats['placements_twin'] += 1
            continue
        seen_twins.add(twin)
        mi = scene_mesh(p['mesh'], p['slots'])
        if mi is None:
            # every part's material skipped or slot empty: the placement draws nothing (scene.json lists them)
            stats['placements_no_material'] += 1
            dropped_meshes[(p['mesh'], tuple(s if s is None else (mats_src.get(s) or {}).get('base') for s in
                                             (p['slots'] or [])))] += 1
            continue
        W = np.zeros((3, 4))
        W[:, :3] = upm * M[:3, :3]
        W[:, 3] = upm * (t - origin)
        near = mesh_near.get(mi, 0.0)
        # terrain foliage's grass and flowers (w3foliage.py: ~110 k clumps on the city's terrain) go at GRASS_HIDE m, as
        # the game fades them out within a few tens of metres; trees, vines and shrubs keep drawing
        gh = upm * GRASS_HIDE if (p.get('component') == 'foliage' and GRASS_RE.search(p['mesh'])) else 0.0
        if ah_mesh and p.get('component') != 'foliage' and not p.get('srt_source'):
            ahd, src = autohide(p, t)
            if ahd is not None:
                # (units, from the sphere's centre: the game's distance scaled, plus the instance's radius)
                r = meshes[mi][1] * float(np.sqrt((M[:3, :3] ** 2).sum(0).max()))
                h = upm * (a.autohide_scale * ahd + r)
                if near > 0 and h <= upm * (near + 40.0):
                    stats['autohide_proxy_inside_near'] += 1     # (a proxy hidden before it draws: kept)
                else:
                    gh = h
                    stats['autohide_' + src] += 1
            else:
                stats['autohide_none'] += 1
        # no projected decals (instance slot bit 25, as Night City's tag_interiors.py flags loose props and foliage) on
        # vegetation and small props (LOD 0 radius under 2.5 m): the sector decals meant for walls and ground drew as
        # hard-edged boxes across vine leaves and slid over barrels and pots (seen in a ground-level fly-through)
        veg = 'vegetation' in p['mesh'].lower() or bool(p.get('srt_source'))
        slot = (1 << 25) if (veg or meshes[mi][1] < 2.5) else 0
        inst.append((W, mi, slot, gh, upm * near))
        inst_layer.append(p.get('layer'))
        inst_veg.append('vegetation' in p['mesh'].lower() or bool(p.get('srt_source')))
        inst_name.append(os.path.basename(p['mesh']).lower())
        stats['instances'] += 1
        stats['instances_far_only'] += 1 if near > 0 else 0

    # proxies: a far-only instance (an entity proxy, near > 0) stands in for the full meshes of its layer inside its box;
    # those get the proxy's near distance as their hide distance, so the mesh dithers out as the proxy dithers in
    # (r_sceneHideFade: a mesh and its proxy at one distance complement each other). Without it both drew past the
    # near distance, the same walls twice at slightly different depths: the fly-through's mottled roofs, dot-screen
    # facades and sweeping "fade-in". Vegetation keeps drawing (trees are
    # not in the shells); where proxies overlap, the farthest near distance wins (a brief double, never a hole)
    if inst:
        C = np.array([W[:, :3] @ (upm * meshes[mi][0]) / upm + W[:, 3] for W, mi, *_ in inst])     # (units)
        hide = np.zeros(len(inst))
        layers = np.array([l or '' for l in inst_layer])
        veg = np.array(inst_veg)
        # what a house shell does not stand for: the ground around it (374 pavements, 415 stairs, 201
        # rocks ... hidden at 55-70 m with nothing in their place)
        ground = np.array([bool(PROXY_KEEP_RE.search(n)) for n in inst_name])
        # each member's world box (a member must lie wholly inside the shell's box, 1 m spare)
        mlo = np.zeros((len(inst), 3))
        mhi = np.zeros((len(inst), 3))
        for k, (W, mi, *_) in enumerate(inst):
            if mi in mesh_box:
                lo, hi = mesh_box[mi]
                cs = np.array([[x, y, z] for x in (lo[0], hi[0]) for y in (lo[1], hi[1]) for z in (lo[2], hi[2])])
                wc = cs @ W[:, :3].T + W[:, 3]
                mlo[k], mhi[k] = wc.min(0), wc.max(0)
            else:
                mlo[k] = mhi[k] = C[k]
        is_proxy = np.array([x[4] > 0 for x in inst])
        owner = np.full(len(inst), -1)      # the proxy whose near distance a member takes (the farthest where they overlap)
        for k in np.nonzero(is_proxy)[0]:
            W, mi, _, _, nearu = inst[k]
            if mi not in mesh_box:
                continue
            lo, hi = mesh_box[mi]
            corners = np.array([[x, y, z] for x in (lo[0], hi[0]) for y in (lo[1], hi[1]) for z in (lo[2], hi[2])])
            wc = corners @ W[:, :3].T + W[:, 3]
            blo, bhi = wc.min(0) - upm, wc.max(0) + upm
            m = (~is_proxy) & (~veg) & (~ground) & (layers == layers[k]) & np.all(mlo >= blo, 1) & np.all(mhi <= bhi, 1)
            take = m & (nearu > hide)
            owner[take] = k
            hide[m] = np.maximum(hide[m], nearu)
        # the member measures its hide distance from its proxy's centre (slot bit 27, the offset in bits 0-23 as a signed
        # byte an axis in 16-unit steps: scene_cull.comp / scene.vert HideCentre), so mesh and proxy fade against one
        # point; each from its own centre (up to 41 m apart) their dithers did not complement
        far_off = 0
        for k in np.nonzero(hide > 0)[0]:
            W, mi, a3, _, nearu = inst[k]
            q = np.round((C[owner[k]] - C[k]) / 16.0).astype(int)
            if np.abs(q).max() > 127 or (a3 & 0x10FFFFFF):
                far_off += 1        # (beyond +-2032 units, or a slot already in use: measured from its own centre)
            else:
                a3 = a3 | (1 << 27) | (int(q[0]) & 255) | ((int(q[1]) & 255) << 8) | ((int(q[2]) & 255) << 16)
            inst[k] = (W, mi, a3, float(hide[k]), nearu)
        stats['instances_hidden_for_proxies'] = int((hide > 0).sum())
        stats['proxy_members_own_centre'] = far_off
        stats['proxies'] = int(is_proxy.sum())

    # terrain: the tiles inside the box as a grid of 64 m meshes, a material per tile (its first ground layer for a
    # renderer without the terrain shader; maps/<map>.xw3t for one with it), the tile UV in the stream's second UV set,
    # the triangles over the game's holes left out (as w3build leaves them out of the collision)
    from w3build import terrain_tiles, solid_share
    tiles = [t + (False,) for t in terrain_tiles(work)]
    near_keys = {t[0] for t in tiles}
    # the ring past the playable area (w3terrain.py into <work>/terrain_far): render only, every --far-step vertex, no
    # box clip; the horizon the fly-through's aerials showed as a flat colour
    tiles += [t + (True,) for t in terrain_tiles(work, 'terrain_far') if t[0] not in near_keys]
    stats['terrain_far_tiles'] = sum(1 for t in tiles if t[-1])
    xw3t_tiles, layer_meta = [], None
    for key, G, props, lattice, bounds, far in tiles:
        if layer_meta is None and props.get('witcher_terrain_layer_metadata'):
            layer_meta = json.loads(props['witcher_terrain_layer_metadata'])
        # (flag 0x1000: terrain, pushed behind the roads, pavements and cobbles laid on it with r_sceneTerrainPush 1; at
        # 37.5 u/m and D24 they lost to it past 200-300 m in the fly-through's aerials)
        tm = mats.flat('terrain_tile_' + key, (0.20, 0.19, 0.13), 0.9, flags=0x1000)
        if lattice is not None:
            if far:     # (the far ring's control map every 8th entry: 400 tiles at 513^2 would add ~200 MB)
                lattice = np.ascontiguousarray(lattice[::8, ::8])
            xw3t_tiles.append((tm, key, lattice, props))
        G = G[::a.far_step, ::a.far_step] if far else G[::a.terrain_step, ::a.terrain_step]
        n_, m_ = G.shape[:2]
        tx0, ty0, tx1, ty1 = bounds
        step = 64
        # normals from the whole tile's grid (central differences; the triangles' winding: cross(along j, along i)),
        # not each chunk's own faces: chunk borders took one-sided normals, a lighting seam every 62.5 m near and 250 m
        # on the far ring
        GN = np.cross(np.gradient(G, axis=1), np.gradient(G, axis=0))
        GN /= np.maximum(np.linalg.norm(GN, axis=-1, keepdims=True), 1e-12)
        for i0 in range(0, n_ - 1, step):
            for j0 in range(0, m_ - 1, step):
                S = G[i0:i0 + step + 1, j0:j0 + step + 1]
                SN = GN[i0:i0 + step + 1, j0:j0 + step + 1].reshape(-1, 3)
                # (every chunk of a near tile: the far ring leaves the near tiles out, so the chunks past the box drew
                # nothing, a 62.5 m x 1 km strip without ground north of the castle)
                sn, sm = S.shape[:2]
                V = S.reshape(-1, 3)
                c = V.mean(0)
                pos = V - c
                ii, jj = np.meshgrid(np.arange(sn - 1), np.arange(sm - 1), indexing='ij')
                q = (ii * sm + jj).ravel()
                tri = np.concatenate([np.stack([q, q + 1, q + sm + 1], 1), np.stack([q, q + sm + 1, q + sm], 1)])
                if lattice is not None:
                    keep = solid_share(lattice, bounds, V[tri].mean(1)[:, :2]) >= 0.5
                    stats['terrain_hole_tris'] += int((~keep).sum())
                    tri = tri[keep]
                if not len(tri):
                    continue
                uv = np.stack([pos[:, 0] / 4.0, -pos[:, 1] / 4.0], -1)
                tuv = np.stack([(V[:, 0] - tx0) / (tx1 - tx0), (V[:, 1] - ty0) / (ty1 - ty0)], -1)
                first_part = len(parts)
                parts.append(geometry(pos, SN, uv, tri, tuv, None, tm) + (tm,))
                lods.append([first_part, 1, 0.0])
                lo, hi = pos.min(0), pos.max(0)
                meshes.append(((lo + hi) / 2, float(np.linalg.norm(hi - lo)) / 2, len(lods) - 1, 1))
                W = np.zeros((3, 4))
                W[:, :3] = upm * np.eye(3)
                W[:, 3] = upm * (c - origin)
                inst.append((W, len(meshes) - 1, 0, 0.0, 0.0))
                stats['terrain_meshes'] += 1

    # water (w3water.py): the sea-level water over every terrain cell under it, the lakes the lilies float on; flat
    # rectangles at each level, a mesh per 250 m tile they fall in (culled apart), the water material. Drawn over the
    # lit bed where it is above it (the renderer's water mode); under the ground the depth test hides it
    if not a.no_water:
        import w3water
        # the game's local water shapes (w3watershapes.py): rivers and lakes at their own levels, and the inland
        # hollows under the sea's level kept dry
        shapes = w3water.load_shapes(work)
        stats['water_shapes'] = len(shapes)
        if not shapes:
            print('w3scene: no %s (w3watershapes.py): the water is the sea and the lily lakes only'
                  % (work / 'water_shapes.json'))
        surfaces = [(w3water.SEA, w3water.sea_rects(tiles, shapes=shapes), 'sea')]
        for level, Q, _s in w3water.shape_quads(tiles, shapes):
            surfaces.append((Q, None, 'shape'))
        # the walls that hold the lakes back (w3water.wall_cells): the triangles of the placed solid meshes (not
        # vegetation) whose instance box reaches the lakes' heights, as boxes in game metres
        lz = [np.array(p['matrix'])[2, 3] for p in pl['placements']
              if p.get('kind') == 'w2mesh' and any(k in p['mesh'].lower() for k in w3water.LILY)]
        walls = None
        if lz:
            zlo, zhi = (min(lz) - 3.0) * upm, (max(lz) + 3.0) * upm      # (units, as the instance boxes)
            boxes = []
            for k in range(len(inst_veg)):
                if inst_veg[k] or mhi[k, 2] < zlo or mlo[k, 2] > zhi:
                    continue
                W, mi = inst[k][0], inst[k][1]
                fl = meshes[mi][2]
                for pi in range(lods[fl][0], lods[fl][0] + lods[fl][1]):
                    P = verts[pi]['pos'].astype(np.float64) @ (W[:, :3] / upm).T + W[:, 3] / upm + origin
                    T = P[idx[pi].reshape(-1, 3)]
                    boxes.append(np.concatenate([T[..., :2].min(1), T[..., :2].max(1), T[..., 2:].min(1),
                                                 T[..., 2:].max(1)], 1))
            walls = np.concatenate(boxes) if boxes else None
            stats['water_wall_triangles'] = 0 if walls is None else len(walls)
        lakes, dropped = w3water.lake_rects(tiles, pl['placements'], walls, shapes)
        surfaces += [(L, rects, 'lake') for L, rects, _rep in lakes]
        stats['water_lakes'] = len(lakes)
        stats['water_lake_groups_dropped'] = len(dropped)
        wm = mats.water()
        for level, rects, what in surfaces:
            if what == 'shape':
                # (a shape's quads carry each corner's level: x0 y0 x1 y1 z00 z10 z11 z01)
                R8 = np.asarray(level, dtype=np.float64).reshape(-1, 8)
            else:
                R = np.array(rects, dtype=np.float64).reshape(-1, 4)
                R8 = np.c_[R, np.full((len(R), 4), level)]
            R = R8[:, :4]
            if not len(R):
                continue
            key = np.floor((R[:, :2] + R[:, 2:]) / 2 / 250.0).astype(int)
            for k in np.unique(key, axis=0):
                sel = (key == k).all(1)
                B = R[sel]
                Bz = R8[sel, 4:]
                n = len(B)
                V = np.zeros((n * 4, 3))
                V[0::4, :2] = B[:, [0, 1]]
                V[1::4, :2] = B[:, [2, 1]]
                V[2::4, :2] = B[:, [2, 3]]
                V[3::4, :2] = B[:, [0, 3]]
                V[0::4, 2], V[1::4, 2], V[2::4, 2], V[3::4, 2] = Bz[:, 0], Bz[:, 1], Bz[:, 2], Bz[:, 3]
                c = V.mean(0)
                pos = V - c
                q = 4 * np.arange(n)[:, None]
                tri = np.concatenate([q + [0, 1, 2], q + [0, 2, 3]]).astype(np.int64)
                uv = np.stack([V[:, 0] / 4.0, -V[:, 1] / 4.0], -1)
                parts.append(geometry(pos, np.tile([0.0, 0.0, 1.0], (len(pos), 1)), uv, tri, mi=wm) + (wm,))
                lods.append([len(parts) - 1, 1, 0.0])
                lo, hi = pos.min(0), pos.max(0)
                meshes.append(((lo + hi) / 2, float(np.linalg.norm(hi - lo)) / 2, len(lods) - 1, 1))
                W = np.zeros((3, 4))
                W[:, :3] = upm * np.eye(3)
                W[:, 3] = upm * (c - origin)
                inst.append((W, len(meshes) - 1, 0, 0.0, 0.0))
                stats['water_meshes_' + what] += 1
            stats['water_m2_' + what] += int(((R[:, 2] - R[:, 0]) * (R[:, 3] - R[:, 1])).sum())

    # maps/<map>.xw3t (vk_scene.cpp Scene_LoadW3Terrain): the layers (values, colour, normal), then per tile its
    # material, lattice, origin and tint
    xw3t = None
    if xw3t_tiles and layer_meta and not a.no_layers:
        import struct
        layers = layer_meta[:32]
        vals, lpaths, npaths = [], [], []
        for L in layers:
            # (version 2: the specular level's values, as the Blender tools' terrain shader takes them)
            vals += [float(L.get('blend_sharpness', 0.0)), float(L.get('slope_base_dampening', 0.0)),
                     float(L.get('slope_normal_dampening', 0.5)), 0.0,
                     float(L.get('specularity', 0.0)), float(L.get('specularity_base', 0.0)), float(L.get('falloff', 0.0)),
                     float(L.get('specularity_scale', 0.0))]
            d = mats.tex.get(L.get('diffuse_dds'))
            nm = mats.tex.get(L.get('normal_dds'))
            lpaths.append(images.add(np.ascontiguousarray(d[..., :3]), 'a') if d is not None else '')
            npaths.append(images.add(dx_to_gl_normal(nm, keep_alpha=True), 'n', alpha=True) if nm is not None else '')
        body = [struct.pack('<4s3I', b'XW3T', 2, len(xw3t_tiles), len(layers)), struct.pack('<%df' % len(vals), *vals)]
        body += [name64(q) for q in lpaths + npaths]
        for tm, key, lattice, props in xw3t_tiles:
            tint_png = (props.get('witcher_terrain_texture_buffer') or '') + '.detail_v5_tint.png'
            tint = mats.tex.get(tint_png) if os.path.isfile(tint_png) else None
            # the add-on's tint image runs top-down (+y first at the bottom): flipped so its first row is the tile's
            # -y edge, as the lattice and the tile UV
            tpath = images.add(np.ascontiguousarray(tint[::-1, :, :3]), 'a') if tint is not None else ''
            m = lattice.shape[0]
            body.append(struct.pack('<2I2f', tm, m, float(origin[0]), float(origin[1])) +
                        name64(tpath) + lattice.astype('<u2').tobytes())
            stats['terrain_tiles_shaded'] += 1
        xw3t = b''.join(body)

    # lights
    lrecs = []
    for L in pl['lights']:
        M = np.array(L['matrix'], dtype=np.float64)
        p = M[:3, 3]
        if not (x0 <= p[0] <= x1 and y0 <= p[1] <= y1):
            continue
        pr = L['props']
        if not pr.get('witcher_light_enabled', True):
            stats['lights_off'] += 1
            continue
        radius = float(pr.get('witcher_light_radius') or 5.0)
        colour = np.array(pr.get('witcher_light_base_color') or L['color'][:3], dtype=np.float64)
        bright = float(pr.get('witcher_light_base_energy') or L['energy']) / (4 * math.pi)
        spot = L['type'] == 'SPOT'
        d = -M[:3, 2]
        d = d / (np.linalg.norm(d) or 1.0)
        half = float(L.get('spot_size', math.pi)) / 2 if spot else math.pi
        inner = half * (1.0 - float(L.get('spot_blend', 0.4))) if spot else half * 0.6
        shadows = str(pr.get('witcher_light_shadow_casting_mode', 'LSCM_None')) not in ('LSCM_None', 'None')
        lrecs.append((upm * (p - origin), upm * radius, colour, bright * a.light_scale, d, math.cos(half),
                      math.cos(inner), (1 if spot else 0) | (4 if shadows else 0), None, a.light_fog,
                      (0.0, 0.0, 0.0, 0), (np.zeros(3), 0.0)))
        stats['lights'] += 1
        stats['lights_shadowed'] += 1 if shadows else 0

    V = np.concatenate(verts) if verts else np.zeros(0, SCENE_VERT)
    I = np.concatenate(idx) if idx else np.zeros(0, '<u4')
    blob = xscn_blob(V, I, meshes, lods, parts, mats.recs, inst, lrecs)
    if a.version == 5:
        # the installed renderer reads 1..5; version 6 only adds the lights' shapes (tubes), which these have none of
        _, _, counts, offs = xscn_head_of(blob)
        body = [(counts[k], blob[offs[k]:offs[k + 1] if k + 1 < len(offs) else len(blob)]) for k in range(9)]
        blob = xscn_rebuild(5, body)
    out = Path(a.out) if a.out else HERE.parents[1] / '.codex-research' / 'levelport' / 'build' / (
        '%s_scene-%s' % (a.map, a.tag))
    out.mkdir(parents=True, exist_ok=True)
    pk3 = out / ('zz_%s_scene.pk3' % a.map)
    images.flush()
    shards = []
    with pk3write.writing(pk3, allowZip64=False) as zf:
        zf.writestr(zipfile.ZipInfo('maps/%s.xscn' % a.map), blob, compress_type=zipfile.ZIP_DEFLATED, compresslevel=1)
        if mats.blend or mats.card or xw3t is not None or wind_of:
            # maps/<map>.xw3b (vk_scene.cpp Scene_LoadW3Blend): header, the stream (12 B a vertex), 1248 B a material;
            # version 3 (with billboard cards) then the card records
            import struct
            S = np.concatenate(stream) if stream else np.zeros((0, 3), np.uint32)
            assert len(S) == len(V), (len(S), len(V))
            recs = []
            for i in range(len(mats.recs)):
                vals, paths = mats.blend.get(i, ([0.0] * 20, [''] * 18))
                f0 = mats.f0.get(i, [0.0, 0.0, 0.0])     # (version 2: 0 keeps the renderer's default 0.04)
                recs.append(struct.pack('<24f', *(list(vals) + list(f0) + [float(mats.kind.get(i, 0))])) +
                            b''.join(name64(q) for q in paths))
            xw3b = (struct.pack('<4s3I', b'XW3B', 4 if wind_of else 3 if mats.card else 2, len(mats.recs), len(V)) +
                    S.astype('<u4').tobytes() + b''.join(recs))
            if mats.card or wind_of:
                # version 3: u32 count, then 416 B a card: u32 type
                # (1 vertical, 2 top), material, views (0 for a top card); f32 a0 (rad), fade0, fade1 (degrees of pitch);
                # u32 pad[2]; f32 uv[16][6] (per view the card's (s, t) to the atlas: u = m0 s + m1 t + m2,
                # v = m3 s + m4 t + m5; unused 0). The pads: --card-match's match to the 3D tree (cardmatch.py, the
                # renderer's Scene_Half): four halves, the albedo's r, g, b multipliers and the sun light's; 0 none
                match = json.loads(Path(a.card_match).read_text()) if a.card_match else {}

                def half(x):
                    return int(np.array([x], np.float16).view(np.uint16)[0])
                body = [struct.pack('<I', len(mats.card))]
                for i, c in sorted(mats.card.items()):
                    mt = match.get(mats.names[i]['name'].split('_', 1)[1])
                    pads = (0, 0) if not mt else (half(mt['k'][0]) | half(mt['k'][1]) << 16,
                                                  half(mt['k'][2]) | half(mt['s']) << 16)
                    stats['billboard_matched'] += 1 if mt else 0
                    uv = [float(x) for m in (c.get('uv') or [])[:16] for x in m]
                    uv += [0.0] * (96 - len(uv))
                    views = int(c.get('views', 0)) if c.get('type') == 1 else 0
                    body.append(struct.pack('<3I3f2I96f', int(c['type']), i, views, float(c.get('a0', 0.0)),
                                            float(a.bb_fade[0]), float(a.bb_fade[1]), *pads, *uv))
                xw3b += b''.join(body)
                stats['billboard_cards'] = len(mats.card)
            if wind_of:
                # version 4: the SpeedTree wind (w3trees.py's wind section; vk_scene_w3.cpp Scene_LoadW3Wind): u32 trees,
                # u32 meshes, then per tree its record (w3trees.WIND_RECORD f32, [0] the options' bits), then per mesh
                # u32 mesh index, u32 tree. Each vertex's wind is in the stream above (its second UV's bits, its colour)
                trees = sorted(set(wind_of.values()))
                tid = {t: k for k, t in enumerate(trees)}
                body = [struct.pack('<2I', len(trees), len(wind_of))]
                for t in trees:
                    r = index[t]['wind']
                    assert len(r) == WIND_RECORD, (t, len(r))
                    body.append(struct.pack('<%df' % WIND_RECORD, *r))
                body += [struct.pack('<2I', m, tid[t]) for m, t in sorted(wind_of.items())]
                xw3b += b''.join(body)
                stats['wind_trees'] = len(trees)
                stats['wind_meshes'] = len(wind_of)
            if mats.ext:
                # XEXT (any version; after the cards and the wind; a renderer without it ignores the file's tail):
                # 'XEXT', u32 materials, u32 EXT_FLOATS, u32 EXT_PATHS, then per material its floats and 64 B paths
                # (all zero: none)
                body = [struct.pack('<4s3I', b'XEXT', len(mats.recs), EXT_FLOATS, EXT_PATHS)]
                for i in range(len(mats.recs)):
                    ev, ep = mats.ext.get(i, ([0.0] * EXT_FLOATS, [''] * EXT_PATHS))
                    body.append(struct.pack('<%df' % EXT_FLOATS, *ev) + b''.join(name64(q) for q in ep))
                xw3b += b''.join(body)
                stats['ext_materials'] = len(mats.ext)
            zf.writestr(zipfile.ZipInfo('maps/%s.xw3b' % a.map), xw3b, compress_type=zipfile.ZIP_DEFLATED, compresslevel=1)
            stats['layer_blend_materials'] = len(mats.blend)
        if xw3t is not None:
            zf.writestr(zipfile.ZipInfo('maps/%s.xw3t' % a.map), xw3t, compress_type=zipfile.ZIP_DEFLATED, compresslevel=1)
        if 'w3_water' in mats.index:
            # maps/<map>.xwat (vk_scene.cpp Scene_LoadWater, version 1): the water material's settings
            import struct
            zf.writestr(zipfile.ZipInfo('maps/%s.xwat' % a.map),
                        struct.pack('<4sIII', b'XWAT', 1, 1, 0) + struct.pack('<I9f', mats.index['w3_water'], *W3_WATER_XWAT)
                        + bytes(64))
        # the images last, rolling over into zz_<map>_scene2.pk3, 3, ... before a pack passes pk3write.LIMIT: the
        # engine's pack reader has no zip64, and a scene past 2 GB in one pack (scene-20, 3.57 GB) stored 971 of its
        # files at zip64 offsets it cannot read
        images.write(_Rollover(zf, out, a.map, shards))
    for s in shards:
        s.close()
        os.replace(s.filename, s.filename[:-len('.tmp')])
    (out / 'materials.json').write_text(json.dumps(mats.names, indent=0), encoding='utf-8')
    stats['missing_images'] = len(mats.tex.missing)
    (out / 'scene.json').write_text(json.dumps({'origin': origin.tolist(), 'box': build['box'],
                                                'missing_images': sorted(mats.tex.missing),
                                                'dropped_meshes': [[m, list(b), n] for (m, b), n in
                                                                   dropped_meshes.most_common()], **stats}, indent=1),
                                    encoding='utf-8')
    tris0 = sum(parts[lods[fl][0] + k][1] // 3 for c, r, fl, nl in meshes for k in range(lods[fl][1]))
    print(f'w3scene -> {pk3} ({pk3.stat().st_size / 1e6:.0f} MB): {len(inst)} instances of {len(meshes)} meshes, '
          f'{len(I) // 3:,} triangles over all LODs ({tris0:,} at LOD 0), {len(mats.recs)} materials, '
          f'{len(images.todo)} images, {len(lrecs)} lights')
    print('  ' + ', '.join(f'{k} {v}' for k, v in sorted(stats.items())))


class _Rollover:
    """writestr into the scene's pack while it stays under pk3write.LIMIT, then into zz_<map>_scene<N>.pk3 shards
    (written as .tmp; main renames them once closed)"""

    def __init__(self, first, out, mapname, shards):
        self.w, self.out, self.map, self.shards = first, Path(out), mapname, shards

    def writestr(self, info, data, compress_type=zipfile.ZIP_STORED):
        if self.w.fp.tell() + len(data) + 4096 > pk3write.LIMIT:
            name = self.out / ('zz_%s_scene%d.pk3.tmp' % (self.map, len(self.shards) + 2))
            self.w = zipfile.ZipFile(name, 'w', allowZip64=False)
            self.shards.append(self.w)
        self.w.writestr(info, data, compress_type=compress_type)


if __name__ == '__main__':
    main()
