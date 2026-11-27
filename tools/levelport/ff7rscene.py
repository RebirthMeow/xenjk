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
"""FF7 Rebirth area -> render scene (maps/<map>.xscn + images, zz_<map>_scene*.pk3) and the build record the gameplay BSP
uses (ff7rbuild.py). First area: Costa del Sol (2110-COSTT), 2026-11-26.

Inputs:
  --extracted  a research extraction (FF7R_Research/Costa_Del_Sol_Extracted): Placements/<map>.json (actor transforms
               from --inspect-world logs), Static_Decoded_v2/<mesh>.gltf (decoded static meshes, metres, Quake axes,
               material slots with their material paths in extras), Skeletal_Props/**.gltf (umodel, Y-up, bind pose),
               Materials/**/<material>.json (CUE4Parse: Textures by role, Parameters.BlendMode), Textures/ and
               Materials/ PNG trees (End/Content/<game path>.png)
  --massive    ff7rmassive.py's output (<stream>.npz/.json: LOD chains, instances, texture lists)
  --blueprints the exporter's --inspect-package dumps of the prop Blueprints and of the maps that place them (class of
               each placed actor; each class's component templates)

Space: game units = source cm x 0.375 (37.5 a metre, levelport), Quake axes (source Y negated), about --origin (cm,
Quake axes; default the centre of --box). Instances outside --box are dropped (10 interior pieces parked 3 km away).

Materials: a Massive mesh's material is its texture list, classified by the name's suffix (_C colour, _N normal,
_R roughness, _O occlusion, _M metal, _A alpha mask; /Game/Renderer/Texture/<RRGGBBAA>_BC4 are constant placeholders);
a static mesh's by its material JSON's texture roles (Color, Normal, Roughness, Metallic, Emissive, Opacity...) and
BlendMode (1 masked -> cutout, 2+ translucent -> blend). Emissive-textured materials draw as the emissive kind with the
emissive image as their colour. Normal maps are DirectX (green down) and turned to OpenGL. ORM packs occlusion,
roughness, metal (defaults 1, 0.6, 0).
"""
import argparse
import collections
import hashlib
import json
import math
import os
import re
import sys
import time
import zipfile
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import gltfio  # noqa: E402
import pk3write  # noqa: E402
from xscn import SCENE_KIND, SCENE_VERT, SceneImages, snorm8, vertex_frame, xscn_blob  # noqa: E402

UPCM = 0.375                     # game units per source centimetre
COSTA_BOX = (180000.0, 45000.0, 270000.0, 170000.0)       # cm, Quake axes: x0 y0 x1 y1
# the sea (main, --no-sea): its level (cm), the tint (sRGB: the colour in the water; Costa's shallows read turquoise
# over the sand, no source value read: the game's ocean is a run-time surface), roughness, the grid (cells of
# SEA_CELL_M metres, SEA_TILE_CELLS a tile) and the .xwat record (cp2gltf WATER_XWAT_FIELDS: visibility m, shore m,
# ripple tile m, speed, heading, roughness (0: the ORM's), normal strength, foam band, foam intensity)
SEA_Z_CM = 0.0
DETAIL_NORMAL = 1.0              # the rocks' tiled detail layer (.xdet): its normal strength and roughness range
DETAIL_ROUGH = 1.0
DECAL_DEPTH_M = 0.3             # a mesh decal reaches this far behind itself onto its receiver (vk_scene.cpp's threshold)
SEA_TINT = (18, 96, 104)
SEA_ROUGH = 0.05
SEA_CELL_M = 100.0
SEA_TILE_CELLS = 8
SEA_XWAT = (6.0, 0.6, 4.0, 0.1, 0.0, 0.0, 0.8, 0.0, 0.0)

AREAS = {
    'costa': {
        'massive': ['2110-COSTT_Terrain_Strip', '2110-COSTT_11Grass_Terrain_Strip', '2110-COSTT_21Tree_Terrain_Strip',
                    '2110-COSTT_31NatureMini_Terrain_Strip', '2110-COSTT_61Interior_Terrain_Strip'],
        'massive_state': {
            'before': ['2110-COSTT_JonnyHotel_Before_Terrain_Strip', '2110-COSTT_12JonnyHotel_Before_Grass_Terrain_Strip',
                       '2110-COSTT_22JonnyHotel_Before_Tree_Terrain_Strip',
                       '2110-COSTT_32JonnyHotelNatureMini_Before_Terrain_Strip',
                       '2110-COSTT_62JonnyHotelInterior_Before_Terrain_Strip'],
            'after': ['2110-COSTT_JonnyHotel_After_Terrain_Strip', '2110-COSTT_13JonnyHotel_After_Grass_Terrain_Strip',
                      '2110-COSTT_23JonnyHotel_After_Tree_Terrain_Strip',
                      '2110-COSTT_33JonnyHotelNatureMini_After_Terrain_Strip',
                      '2110-COSTT_63JonnyHotelInterior_After_Terrain_Strip']},
        # the land around the town (--surround: the Corel region's and the western continent's streams, decoded by
        # ff7rmassive.py from their own extraction): instances out to --surround-margin past the box, grass to
        # --surround-grass-margin, so the hills behind the town and the road out past the chocobo stop do not end at
        # the box
        'surround': ['2200-CORLA_NorthCorel_Terrain_Strip', '2200-CORLA_NorthCorelGrass_Terrain_Strip',
                     '2200-CORLA_24NorthCorelTree_Terrain_Strip', '2200-CORLA_34NorthCorelNatureMini_Terrain_Strip',
                     '2200-CORLA_64NorthCorelInterior_Terrain_Strip', '2200-CORLA_DesertLine_Terrain_Strip',
                     '2200-CORLA_22DesertLineTree_Terrain_Strip', '2200-CORLA_32DesertLineNatureMini_Terrain_Strip',
                     '2200-CORLA_SideSea_Terrain_Strip', '2200-CORLA_37SideSeaNatureMini_Terrain_Strip',
                     '2000-WESTC_TinyBRoute_C_Terrain_Strip', '2000-WESTC_TinyBRoute_C_21Tree_Terrain_Strip',
                     '2000-WESTC_TinyBRoute_B_Terrain_Strip'],
        # the surround's far stand-ins (Simplygon meshes, one a FarLand map, placed at the origin; their own baked
        # maps): drawn past --farland-near, where the surround streams' instances stop (the game's own far view)
        'farland': ['2000-WESTC_TinyBRoute_B_Terrain_FarLand', '2000-WESTC_TinyBRoute_C_Terrain_FarLand',
                    '2000-WESTC_TinyBRoute_Terrain_FarLand', '2200-CORLA_CorelCave_Terrain_FarLand',
                    '2200-CORLA_DesertLine_Terrain_FarLand', '2200-CORLA_NorthCorel_Terrain_FarLand',
                    '2200-CORLA_SideGongaga_Terrain_FarLand', '2200-CORLA_SideMountain_Terrain_FarLand',
                    '2200-CORLA_SideSeaFar_Terrain_FarLand', '2200-CORLA_SideSea_Terrain_FarLand'],
        'placed': ['2110-COSTT_Terrain_Strip', '2110-COSTT_Gimmick', '2110-COSTT_Emissive', '2110-COSTT_Light',
                   '2110-COSTT_Physics', '2110-COSTT_Water_Terrain', '2100-COSTA_Gimmick',
                   '2110-COSTT_Terrain_FarLand'],
        'timeset': '2110-COSTT_TimeSet_%s',
        # the end-game state (with --state after): the beach open (its closed state's fence poles and debris decal
        # off), Johnny's after signboard, the trophies and collection on show (their "getting ready" boards off), the
        # second shooting game's masts, the Shinra boat after, the piano unfenced, the ladder up, the rare crown; no
        # boss-fight walls or cutscene-only props (EV_*)
        'hide_states': ('BGFA2110_PrivateBeach', 'BGFA2110_SeaSideJohnnySignboardBefore', 'BGFA2110_Ready_Collection',
                        'BGFA2110_Ready_Trophy', 'BGFA2110_FirstMastL', 'BGFA2110_FirstMastS',
                        'BGFA2100_Shinramaru_Before', 'BGFA2110_PianoFence', 'BGFA2110_LadderOff',
                        'BGFA2110_Crown_Normal', 'BGFA2110_BossBattleCollision', 'EV_*'),
        'placed_state': {
            'before': ['2110-COSTT_JonnyHotel_Before_Terrain_Strip', '2110-COSTT_JonnyHotel_Before_Light',
                       '2110-COSTT_JonnyHotel_Before_Gimmick', '2110-COSTT_JonnyHotel_Before_Terrain_FarLand'],
            'after': ['2110-COSTT_JonnyHotel_After_Terrain_Strip', '2110-COSTT_JonnyHotel_After_Light',
                      '2110-COSTT_JonnyHotel_After_Terrain_FarLand']},
    },
}


# ----------------------------------------------------------------------------------------------------- transforms

def rotator_matrix(pitch, yaw, roll):
    """UE FRotator -> 3x3 whose columns are the local X, Y, Z axes in UE world space"""
    p, y, r = (math.radians(v) for v in (pitch, yaw, roll))
    sp, cp, sy, cy, sr, cr = math.sin(p), math.cos(p), math.sin(y), math.cos(y), math.sin(r), math.cos(r)
    x_axis = (cp * cy, cp * sy, sp)
    y_axis = (sr * sp * cy - cr * sy, sr * sp * sy + cr * cy, -sr * cp)
    z_axis = (-(cr * sp * cy + sr * sy), cy * sr - cr * sp * sy, cr * cp)
    return np.array([x_axis, y_axis, z_axis], np.float64).T


S = np.diag([1.0, -1.0, 1.0])


def ue_to_quake(loc, rot, scale):
    """A UE transform (cm, rotator, scale) as a 4x4 in Quake axes, cm"""
    M = np.eye(4)
    M[:3, :3] = S @ rotator_matrix(rot['Pitch'], rot['Yaw'], rot['Roll']) @ S @ np.diag(scale)
    M[:3, 3] = (loc[0], -loc[1], loc[2])
    return M


def quat_matrix(q):
    w, x, y, z = q
    n = math.sqrt(w * w + x * x + y * y + z * z) or 1.0
    w, x, y, z = w / n, x / n, y / n, z / n
    return np.array([[1 - 2 * (y * y + z * z), 2 * (x * y - w * z), 2 * (x * z + w * y)],
                     [2 * (x * y + w * z), 1 - 2 * (x * x + z * z), 2 * (y * z - w * x)],
                     [2 * (x * z - w * y), 2 * (y * z + w * x), 1 - 2 * (x * x + y * y)]])


# ----------------------------------------------------------------------------------------------------- textures

class TexFinder:
    """A game texture path -> its PNG in one of the extraction's trees; constant placeholders as values"""

    def __init__(self, roots):
        self.roots = [Path(r) for r in roots]
        self.cache = collections.OrderedDict()

    def file(self, game_path):
        rel = re.sub(r'\.[^/]*$', '', game_path).lstrip('/')
        if rel.startswith('Game/'):
            rel = 'End/Content/' + rel[5:]
        for r in self.roots:
            for q in (rel, rel[12:] if rel.startswith('End/Content/') else None):   # (umodel's tree drops End/Content)
                if q and (r / (q + '.png')).exists():
                    return r / (q + '.png')
        return None

    @staticmethod
    def constant(game_path):
        m = re.match(r'/(?:Game|End/Content)/Renderer/Texture/([0-9A-Fa-f]{8})_', game_path or '')
        return tuple(int(m.group(1)[k:k + 2], 16) for k in (0, 2, 4, 6)) if m else None

    def load(self, game_path, cap):
        key = (game_path, cap)
        if key in self.cache:
            self.cache.move_to_end(key)
            return self.cache[key]
        f = self.file(game_path)
        img = None
        if f is not None:
            from PIL import Image
            im = Image.open(f)
            im.load()
            if max(im.size) > cap:
                s = cap / max(im.size)
                im = im.resize((max(1, int(im.size[0] * s)), max(1, int(im.size[1] * s))), Image.Resampling.LANCZOS)
            img = np.asarray(im.convert('RGBA') if im.mode not in ('RGBA', 'RGB', 'L') else im)
            if img.ndim == 2:
                img = np.repeat(img[..., None], 3, axis=2)
        self.cache[key] = img
        while len(self.cache) > 64:
            self.cache.popitem(last=False)
        return img


def suffix_role(path):
    name = re.sub(r'\.[^/]*$', '', path or '').rsplit('/', 1)[-1]
    suf = name.rsplit('_', 1)[-1] if '_' in name else ''
    return {'C': 'color', 'CM': 'color', 'N': 'normal', 'R': 'rough', 'O': 'occ', 'M': 'metal', 'A': 'alpha', 'EM': 'emissive',
            'E': 'emissive', 'T': 'alpha', 'Op': 'alpha'}.get(suf)


# (Detail*: the terrain materials' layer, after the primaries: M_Trn_2110_GroundBeach_01AA's Color is the white
# placeholder, its sand in DetailColor and PM_Diffuse -- the beach drew white, look-3)
JSON_ROLES = [('color', ('Color', 'PM_Diffuse', 'BaseColor', 'Diffuse', 'Albedo', 'DetailColor')),
              ('normal', ('Normal', 'PM_Normals', 'Normals', 'DetailNormal')),
              ('rough', ('Roughness', 'PM_SpecularMasks', 'DetailRoughness')),
              ('metal', ('Metallic', 'Metalness')),
              ('occ', ('Occlusion', 'AO', 'AmbientOcclusion', 'DetailOcclusion')),
              ('emissive', ('Emissive', 'PM_Emissive', 'EmissiveColor')),
              ('alpha', ('Opacity', 'OpacityMask', 'Alpha', 'Mask'))]


def resize_to(img, shape):
    if img.shape[:2] == shape:
        return img
    from PIL import Image
    return np.asarray(Image.fromarray(img).resize((shape[1], shape[0]), Image.Resampling.BILINEAR))


class Materials:
    def __init__(self, map_name, tex, cache, tex_size, orm_size, emissive):
        self.images = SceneImages(map_name, cache)
        self.tex, self.size, self.orm_size, self.emissive = tex, tex_size, orm_size, emissive
        self.recs, self.index, self.info = [], {}, []
        self.stats = collections.Counter()
        self.detail = {}            # material index -> (detail image path, tiles per UV unit): maps/<map>.xdet
        self.detail_images = {}     # tile set -> its detail image path
        self.memo = MaterialMemo(cache, self)

    def stamp(self, roles):
        """The inputs a material's images come from: each role's source file (path, size, time) or its constant"""
        out = []
        for r, p in sorted(roles.items()):
            f = self.tex.file(p) if p and self.tex.constant(p) is None else None
            st = f.stat() if f is not None else None
            out.append((r, p, str(f) if f else None, st.st_size if st else None, st.st_mtime_ns if st else None))
        return out

    def add_detail(self, i, tile, metres_per_uv):
        """Material i's world-tiled detail layer (massive_tile's set) at metres_per_uv: one .xdet v2 slot over its whole
        surface, as cp77mat's detail_image: rg the tile normal's slope (OpenGL), b its brightness over its mean / 2,
        a its roughness over its mean + 0.5."""
        if i in self.detail or tile is None or not metres_per_uv:
            return
        roles, metres = tile
        key = tuple(sorted(roles.items()))
        if key not in self.detail_images:
            mkey = self.memo.key('detail', self.stamp(roles), self.size, self.orm_size)
            hit = self.memo.get(mkey)
            if hit is not None:
                self.detail_images[key] = hit['path']
                self.stats.update(hit['stats'])
        if key not in self.detail_images:
            stats0 = collections.Counter(self.stats)
            T = self.tex
            nt = T.load(roles['normal'], self.size) if roles.get('normal') else None
            ct = T.load(roles['color'], self.size) if roles.get('color') else None
            if nt is None and ct is None:
                self.detail_images[key] = None
            else:
                s = (nt if nt is not None else ct).shape[0]
                out = np.full((s, s, 4), 0.5, np.float32)
                if nt is not None:
                    n = resize_to(np.ascontiguousarray(nt[..., :3]), (s, s)).astype(np.float32) / 255.0
                    out[..., 0], out[..., 1] = n[..., 0], 1.0 - n[..., 1]       # DirectX green down -> up
                if ct is not None:
                    c = (resize_to(np.ascontiguousarray(ct[..., :3]), (s, s)).astype(np.float32) / 255.0) ** 2.2
                    lum = c @ np.array([0.2126, 0.7152, 0.0722], np.float32)
                    out[..., 2] = lum / max(float(lum.mean()), 1e-4) * 0.5
                rt = T.load(roles['rough'], self.orm_size) if roles.get('rough') else None
                if rt is not None:
                    r = resize_to(np.ascontiguousarray(rt[..., 0]), (s, s)).astype(np.float32) / 255.0
                    out[..., 3] = r - float(r.mean()) + 0.5
                img = np.uint8(np.round(np.clip(out, 0, 1) * 255))
                self.detail_images[key] = self.images.add(img, 'd', alpha=True)
                self.stats['detail_images'] += 1
            self.memo.put(mkey, {'path': self.detail_images[key], 'stats': dict(self.stats - stats0)})
        path = self.detail_images[key]
        if path:
            self.detail[i] = (path, metres_per_uv / metres)
            self.stats['materials_detail'] += 1

    def xdet(self):
        """maps/<map>.xdet version 2 (vk_scene.cpp Scene_LoadDetail): every material's detail as one slot under a
        constant full share map, or none."""
        if not self.detail:
            return None
        import struct
        share = self.images.add(np.tile(np.array([255, 0, 0, 0], np.uint8), (4, 4, 1)), 's', alpha=True)
        table = {}
        for path, _ in self.detail.values():
            table.setdefault(path, len(table))
        out = [struct.pack('<4s3I', b'XDET', 2, len(self.recs), len(table))]
        out += [p.encode('ascii').ljust(64, b'\0') for p in sorted(table, key=table.get)]
        empty = struct.pack('<I4f', 0xFFFFFFFF, 0.0, 0.0, 0.0, 0.0) * 8
        for i in range(len(self.recs)):
            d = self.detail.get(i)
            if d is None:
                out.append(empty + bytes(128))
                continue
            out.append(struct.pack('<I4f', table[d[0]], d[1], DETAIL_NORMAL, DETAIL_ROUGH, 1.0) + empty[20:]
                       + share.encode('ascii').ljust(64, b'\0') + bytes(64))
        return b''.join(out)

    def add(self, key, roles, blend, name):
        """roles: {role: game path}; blend: 'opaque'|'cutout'|'blend'. One record per distinct key."""
        if key in self.index:
            return self.index[key]
        mkey = self.memo.key('add', self.stamp(roles), blend, self.size, self.orm_size, self.emissive)
        hit = self.memo.get(mkey)
        if hit is not None:
            i = len(self.recs)
            self.recs.append((hit['paths'], hit['skind'], hit['flags'], hit['em'], hit['tint'], hit['window']))
            self.info.append({'index': i, 'name': name, 'mesh': str(name), 'kind': hit['kind'], 'roles': roles,
                              'images': hit['paths']})
            self.index[key] = i
            self.stats.update(hit['stats'])
            return i
        stats0 = collections.Counter(self.stats)
        T = self.tex
        def img(role, cap):
            p = roles.get(role)
            if not p or T.constant(p) is not None:
                return None
            got = T.load(p, cap)
            if got is None:
                self.stats['missing_' + role] += 1
            return got
        def const(role, default):
            c = T.constant(roles.get(role) or '')
            return c[0] / 255.0 if c else default
        col = img('color', self.size)
        emi = img('emissive', self.size)
        kind = blend
        # a mesh decal (Rebirth's T_Decal_* sets: footprints, cracks, ground stains on the sand) projects onto the
        # surface under it in the renderer's decal pass (flag 0x40) instead of standing as its own cut-out surface;
        # where its colour is the receiver's own texture (the footprints carry the beach's) it only changes the normal
        decal = any('/T_Decal_' in (roles.get(r) or '') for r in ('alpha', 'normal'))
        diffuse = 1.0 if '/T_Decal_' in (roles.get('color') or '') else 0.0
        if decal:
            kind = 'blend'
        alpha = img('alpha', self.size) if blend != 'opaque' or roles.get('alpha') else None
        if alpha is not None and blend == 'opaque' and not decal:
            kind = 'cutout'
        if col is None:
            c = T.constant(roles.get('color') or '')
            if c:                               # (a constant albedo: Massive slot 0, 767676FF_sRGB)
                col = np.full((4, 4, 3), c[:3], np.uint8)
                self.stats['constant_colour'] += 1
            else:
                g = int(round(255 * 0.5 ** (1 / 2.2)))
                col = np.full((4, 4, 3), g, np.uint8)
                self.stats['no_colour'] += 1
        paths = ['', '', '']
        if emi is not None and kind == 'opaque':
            kind = 'emissive'
            paths[0] = self.images.add(np.ascontiguousarray(emi[..., :3]), 'a')
        else:
            if kind in ('cutout', 'blend'):
                rgba = col if col.shape[2] == 4 else np.concatenate([col[..., :3], np.full(col.shape[:2] + (1,), 255, np.uint8)], -1)
                if alpha is not None:
                    a = resize_to(np.ascontiguousarray(alpha[..., 0]), rgba.shape[:2])
                    rgba = np.concatenate([rgba[..., :3], a[..., None]], -1)
                paths[0] = self.images.add(np.ascontiguousarray(rgba), 'a', alpha=True, coverage=kind == 'cutout')
            else:
                paths[0] = self.images.add(np.ascontiguousarray(col[..., :3]), 'a')
        nrm = img('normal', self.size)
        if nrm is not None:
            n = nrm[..., :3].astype(np.float32) / 127.5 - 1.0
            n[..., 1] = -n[..., 1]                                     # DirectX -> OpenGL
            n[..., 2] = np.sqrt(np.clip(1.0 - n[..., 0] ** 2 - n[..., 1] ** 2, 0.0, 1.0))
            paths[1] = self.images.add(np.uint8(np.clip(np.round((n + 1.0) * 127.5), 0, 255)), 'n')
        r, o, m = img('rough', self.orm_size), img('occ', self.orm_size), img('metal', self.orm_size)
        if r is None and roles.get('rough_mean'):
            # (its tiled detail set's roughness, as a constant: at the tile's own rate it rides on the detail layer)
            t = T.load(roles['rough_mean'], 64)
            if t is not None:
                r = np.full((4, 4, 1), int(round(float(t[..., 0].mean()))), np.uint8)
        shape = next((x.shape[:2] for x in (r, o, m) if x is not None), (4, 4))
        ch = lambda x, d: (resize_to(np.ascontiguousarray(x[..., 0]), shape) if x is not None
                           else np.full(shape, int(round(255 * d)), np.uint8))
        orm = np.stack([ch(o, const('occ', 1.0)), ch(r, const('rough', 0.6)), ch(m, const('metal', 0.0))], -1)
        paths[2] = self.images.add(np.ascontiguousarray(orm), 'o')
        flags = 1 if kind in ('cutout', 'blend') else 0
        em = [self.emissive, self.emissive, self.emissive, 1.0] if kind == 'emissive' else [1.0, 1.0, 1.0, 0.0]
        tint, window = [1.0, 1.0, 1.0, 1.0], [0.0, 0.0]
        if decal:
            flags |= 0x40
            tint[3], window = diffuse, [DECAL_DEPTH_M, 1.0]
            self.stats['materials_decal'] += 1
        i = len(self.recs)
        self.recs.append((paths, SCENE_KIND[kind], flags, em, tint, window))
        # ('mesh': what levelcheck's load_scene judges floors by, NOT_FLOOR)
        self.info.append({'index': i, 'name': name, 'mesh': str(name), 'kind': kind, 'roles': roles, 'images': paths})
        self.index[key] = i
        self.stats['materials_' + kind] += 1
        self.memo.put(mkey, {'paths': paths, 'skind': SCENE_KIND[kind], 'kind': kind, 'flags': flags, 'em': em,
                             'tint': tint, 'window': window, 'stats': dict(self.stats - stats0)})
        return i


class MaterialMemo:
    """A material's finished images by its inputs, kept beside the image cache (matmemo.json): a rebuild had decoded
    and resized every source PNG only to name the DDS the cache already held (316 of a 399 s build's seconds). The key
    is the inputs (the role files' paths, sizes and times, the options) and the image code's own source: an edit to how
    an image is made misses every entry. A hit stands only while each of its images is still in the cache."""

    def __init__(self, cache, mats):
        import inspect
        self.mats, self.path, self.data, self.dirty = mats, (Path(cache) / 'matmemo.json') if cache else None, {}, False
        code = ''.join(inspect.getsource(o) for o in (Materials.add, Materials.add_detail, TexFinder, resize_to,
                                                         SceneImages.add))
        self.code = hashlib.sha1(code.encode('utf-8')).hexdigest()[:12]
        if self.path is not None and self.path.exists():
            try:
                self.data = json.loads(self.path.read_text(encoding='utf-8'))
            except ValueError:
                self.data = {}

    def key(self, *parts):
        return hashlib.sha1(repr((self.code,) + parts).encode('utf-8')).hexdigest()

    def get(self, k):
        if self.path is None:
            return None
        hit = self.data.get(k)
        if hit is None:
            return None
        imgs = [p for p in (hit.get('paths') or [hit.get('path')]) if p]
        if not all(self.mats.images.cached(p).exists() for p in imgs):
            return None
        for p in imgs:
            self.mats.images.keep(p)
        self.mats.stats['memo_hits'] += 1
        return hit

    def put(self, k, v):
        if self.path is not None:
            self.data[k] = v
            self.dirty = True

    def save(self):
        """After the images are flushed (a hit's images must be in the cache)"""
        if self.path is not None and self.dirty:
            tmp = self.path.with_suffix('.tmp%d' % os.getpid())
            tmp.write_text(json.dumps(self.data), encoding='utf-8')
            os.replace(tmp, self.path)
            self.dirty = False


def state_key_hidden(key, hide):
    """Whether an EnvironmentAccessor key is one of hide's: the same name, or it with a number after
    (BGFA2110_PrivateBeach05); an entry ending in * matches as a prefix."""
    for k in hide:
        if k.endswith('*'):
            if key.startswith(k[:-1]):
                return True
        elif key == k or (key.startswith(k) and key[len(k):].isdigit()):
            return True
    return False


def massive_tile(textures):
    """A Massive material's world-tiled detail set (its T_Tile_* textures, e.g. T_Tile_Limestone_8m_02D_N): {role:
    path} for colour, normal and roughness, and the tile's size in metres from its name; None without a tiled normal or
    colour."""
    if not any(not TexFinder.constant(p) and not is_tile(p) for p in textures):
        return None             # (the tile is the material's own surface)
    roles, metres = {}, None
    for p in textures:
        stem = p.rsplit('/', 1)[-1].split('.')[0]
        if not stem.startswith('T_Tile_') or TexFinder.constant(p):
            continue
        r = suffix_role(p)
        if r in ('color', 'normal', 'rough') and r not in roles:
            roles[r] = p
            m = re.search(r'_(\d+(?:\.\d+)?)m_', stem)
            metres = metres or (float(m.group(1)) if m else None)
    return (roles, metres) if ('normal' in roles or 'color' in roles) and metres else None


def uv_metres(pos_cm, uv, tris):
    """Metres of surface per UV unit (the median over the triangles of sqrt(world area / UV area)); None if flat."""
    p, t = pos_cm[tris] / 100.0, uv[tris][..., :2]
    wa = np.linalg.norm(np.cross(p[:, 1] - p[:, 0], p[:, 2] - p[:, 0]), axis=1)
    e1, e2 = t[:, 1] - t[:, 0], t[:, 2] - t[:, 0]
    ua = np.abs(e1[:, 0] * e2[:, 1] - e1[:, 1] * e2[:, 0])
    ok = (ua > 1e-10) & (wa > 1e-8)
    return float(np.median(np.sqrt(wa[ok] / ua[ok]))) if ok.sum() >= 8 else None


def is_tile(p):
    return p.rsplit('/', 1)[-1].startswith('T_Tile_')


def massive_roles(textures):
    """A Massive material's roles from its texture slots. Its own (unique) maps first; a world-tiled T_Tile_* set fills a
    role only when the material has no own maps (a town floor made of the tile alone), else it is the detail layer
    (massive_tile) and a role its own maps lack takes the tile's mean (roughness: 'rough_mean'). Slot 0 is the albedo's:
    a constant there (767676FF_sRGB) is the colour when no colour map follows."""
    roles = {}
    own = [p for p in textures if not TexFinder.constant(p) and not is_tile(p)]
    for p in own + ([] if own else [p for p in textures if is_tile(p)]):
        r = suffix_role(p) if not TexFinder.constant(p) else None
        if r and r not in roles:
            roles[r] = p
    if own and 'rough' not in roles:
        tr = next((p for p in textures if is_tile(p) and suffix_role(p) == 'rough'), None)
        if tr:
            roles['rough_mean'] = tr
    if 'color' not in roles and textures and TexFinder.constant(textures[0]) and textures[0].endswith('_sRGB'):
        roles['color'] = textures[0]
    return roles


class MaterialJson:
    def __init__(self, roots):
        self.by_name = {}
        for r in roots:
            for p in Path(r).rglob('*.json'):
                self.by_name.setdefault(p.stem, p)
            for p in Path(r).rglob('*.props.txt'):        # umodel's (the skeletal props' materials)
                self.by_name.setdefault(p.name[:-len('.props.txt')], p)

    def roles(self, material_path_or_name):
        name = re.sub(r'\.[^/]*$', '', material_path_or_name or '').rsplit('/', 1)[-1]
        p = self.by_name.get(name)
        if p is None:
            return None, 'opaque'
        if p.name.endswith('.props.txt'):
            return self.props_roles(p)
        d = json.loads(p.read_text(encoding='utf-8'))
        tex = d.get('Textures') or {}
        roles = {}
        for role, keys in JSON_ROLES:
            got = [tex[k] for k in keys if tex.get(k)]
            # (a texture beats a constant placeholder)
            real = [v for v in got if not TexFinder.constant(v)]
            if got:
                roles[role] = (real or got)[0]
        for v in tex.values():
            r = suffix_role(v)
            if r and r not in roles and not TexFinder.constant(v):
                roles[r] = v
        bm = (d.get('Parameters') or {}).get('BlendMode', 0)
        return roles, ('cutout' if bm == 1 else 'blend' if bm and bm >= 2 else 'opaque')


    @staticmethod
    def props_roles(p):
        """umodel's .props.txt: TextureParameterValues (ParameterInfo Name, then ParameterValue Texture2D'path'),
        BlendMode = BLEND_*"""
        txt = p.read_text(encoding='utf-8', errors='replace')
        tex = dict(re.findall(r"Name=(\w+) \}\s*ParameterValue = Texture2D'([^']+)'", txt))
        roles = {}
        for role, keys in JSON_ROLES + [('alpha', ('Coverage',))]:
            for k in keys:
                if tex.get(k) and not TexFinder.constant(tex[k]) and role not in roles:
                    roles[role] = tex[k]
        bm = re.search(r'^\s*BlendMode = BLEND_(\w+)', txt, re.M)
        bm = bm.group(1) if bm else 'Opaque'
        return roles, ('cutout' if bm == 'Masked' else 'opaque' if bm == 'Opaque' else 'blend')


# ----------------------------------------------------------------------------------------------------- meshes

class Scene:
    def __init__(self):
        self.V, self.I, self.meshes, self.lods, self.parts, self.inst = [], [], [], [], [], []
        self.nv = self.ni = 0
        self.mesh_index = {}

    def add_mesh(self, key, lod_parts, screens, density=None):
        """lod_parts: [[(pos units [N,3], nrm or None, uv [N,2], tris [M,3], material)]] per LOD; density: a floor
        under each LOD's screen but the last, from its triangles (lod_density)"""
        lo, hi = np.full(3, np.inf), np.full(3, -np.inf)
        first_lod = len(self.lods)
        if density:
            screens = lod_density(screens, [sum(len(p[3]) for p in parts) for parts in lod_parts], density)
        for parts, sc in zip(lod_parts, screens):
            first_part = len(self.parts)
            for pos, nrm, uv, tris, mat in parts:
                if not len(tris):
                    continue
                n, t = vertex_frame(pos.astype(np.float64), None if nrm is None else nrm.astype(np.float64),
                                    None if uv is None else uv.astype(np.float64), tris)
                v = np.zeros(len(pos), SCENE_VERT)
                v['pos'] = pos
                v['nrm'][:, :3] = snorm8(n)
                v['tan'] = snorm8(t)
                v['uv'] = uv if uv is not None else 0.0
                self.V.append(v)
                self.I.append(tris.astype('<u4').ravel())
                self.parts.append((self.ni, tris.size, self.nv, mat))
                self.nv += len(pos)
                self.ni += tris.size
                lo, hi = np.minimum(lo, pos.min(0)), np.maximum(hi, pos.max(0))
            self.lods.append((first_part, len(self.parts) - first_part, sc))
        if not np.isfinite(lo).all():
            self.lods = self.lods[:first_lod]
            self.mesh_index[key] = None
            return None
        c = (lo + hi) / 2
        self.meshes.append((c, float(np.linalg.norm(hi - lo)) / 2, first_lod, len(self.lods) - first_lod))
        self.mesh_index[key] = len(self.meshes) - 1
        return self.mesh_index[key]

    def place(self, mesh, W, hide=0.0, near=0.0):
        """hide: not drawn past this eye distance; near: nor within it (scene units, 0: none)"""
        if hide or near:
            self.inst.append((W[:3, :4].astype(np.float32), mesh, 0, float(hide), float(near)))
        else:
            self.inst.append((W[:3, :4].astype(np.float32), mesh, 0))


def lod_screens(n, base):
    return [base * 0.5 ** k if k < n - 1 else 0.0 for k in range(n)]


def lod_density(screens, tris, density, rows=1080.0):
    """screens raised so a LOD is drawn only while it has at most density triangles a pixel of its projected
    diameter squared at rows view rows (the renderer's LOD size: projected diameter / view height). The halving chain
    alone kept the surround's cliffs (187 k triangles at LOD 0, 80 m across) at 49 k triangles where they stood 80
    pixels tall: Costa drew 22-33 M triangles a frame, Beauclair 1-2 M. A small prop's floor is under its chain's
    screen and changes nothing; the last LOD stays the fallback"""
    out = list(screens)
    for k in range(len(out) - 1):
        out[k] = max(out[k], (tris[k] / density) ** 0.5 / rows)
    for k in range(len(out) - 2, -1, -1):          # (non-increasing: the renderer takes the first LOD that fits)
        out[k] = max(out[k], out[k + 1])
    return out


# ----------------------------------------------------------------------------------------------------- blueprints

def blueprint_classes(log):
    """{actor path: class name} from an --inspect-package dump of the maps"""
    out = {}
    for line in open(log, encoding='utf-8', errors='replace'):
        m = re.match(r'^EXPORT index=\d+ path=(\S+) exportType=(\S+_C) ', line)
        if m:
            out[m.group(1)] = m.group(2)
    return out


def blueprint_parts(log):
    """{class name: [(kind, mesh path, rel loc, rel rot, rel scale)]} from an --inspect-package dump of the classes:
    their construction templates (CDO components and SCS *_GEN_VARIABLE) with a static or skeletal mesh, except the
    physics break pieces (EndStaticMeshPhysicsPartsComponent: hidden until the prop breaks)"""
    out, cur, comps = {}, None, {}
    def flush():
        if cur:
            out[cur] = [c for c in comps.values() if c.get('mesh')]
    for line in open(log, encoding='utf-8', errors='replace'):
        m = re.match(r'^PACKAGE object=\S+\.(\S+_C) ', line)
        if m:
            flush()
            cur, comps = m.group(1), {}
            continue
        m = re.match(r'^EXPORT index=(\d+) path=\S+ exportType=(\S+) ', line)
        if m:
            i, et = m.group(1), m.group(2)
            if 'PhysicsParts' not in et and ('StaticMeshComponent' in et or 'SkeletalMeshComponent' in et):
                comps[i] = {'kind': 'skeletal' if 'Skeletal' in et else 'static', 'loc': [0, 0, 0],
                            'rot': {'Pitch': 0, 'Yaw': 0, 'Roll': 0}, 'scale': [1, 1, 1]}
            continue
        m = re.match(r'^EXPORT_PROPERTY index=(\d+) (\w+) (.*)$', line)
        if m and m.group(1) in comps:
            k, raw = m.group(2), m.group(3)
            try:
                tag = json.loads(raw).get('Tag')
            except json.JSONDecodeError:
                continue
            c = comps[m.group(1)]
            if k in ('StaticMesh', 'SkeletalMesh', 'SkinnedAsset') and isinstance(tag, dict) and tag.get('ObjectPath'):
                c['mesh'] = re.sub(r'\.\d+$', '', tag['ObjectPath'])
            elif k == 'RelativeLocation':
                c['loc'] = [tag['X'], tag['Y'], tag['Z']]
            elif k == 'RelativeRotation':
                c['rot'] = tag
            elif k == 'RelativeScale3D':
                c['scale'] = [tag['X'], tag['Y'], tag['Z']]
    flush()
    return out


# ----------------------------------------------------------------------------------------------------- lights

def kelvin_rgb(k):
    """Blackbody colour (linear, max 1) for k kelvin (Tanner Helland's fit, then linearised)"""
    t = k / 100.0
    r = 255.0 if t <= 66 else 329.698727446 * (t - 60) ** -0.1332047592
    g = 99.4708025861 * math.log(t) - 161.1195681661 if t <= 66 else 288.1221695283 * (t - 60) ** -0.0755148492
    b = 255.0 if t >= 66 else 0.0 if t <= 19 else 138.5177312231 * math.log(t - 10) - 305.0447927307
    c = np.clip(np.array([r, g, b]) / 255.0, 0, 1) ** 2.2
    return c / max(c.max(), 1e-6)


def srgb_linear(c):
    c = np.asarray(c, np.float64) / 255.0
    return np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)


SKIP_LIGHTS = ('_shoot_',)     # the shooting minigame's stage lights (switched on for it alone)


def attach_parents(log):
    """{(actor, component): (parent actor, parent component)} from an --inspect-world log (the placements keep only the
    parent's package path; its ObjectName names it)"""
    out, cur = {}, None
    if not Path(log).exists():
        return out
    for line in open(log, encoding='utf-8', errors='replace'):
        if line.startswith('COMPONENT '):
            path = line[10:].split(' runtime=', 1)[0].rsplit('PersistentLevel.', 1)[-1]
            cur = tuple(path.split('.', 1)) if '.' in path else None
        elif line.startswith('COMPONENT_PROPERTY AttachParent ') and cur:
            try:
                name = json.loads(line.split(' ', 2)[2])['Tag']['ObjectName']
            except (json.JSONDecodeError, KeyError, TypeError):
                continue
            m = re.search(r"PersistentLevel\.([^.']+)\.([^.']+)'", name or '')
            if m:
                out[cur] = (m.group(1), m.group(2))
        elif line.startswith('ACTOR '):
            cur = None
    return out


def light_record(rec, M, origin, scale, fog):
    """A Placements light (its world transform M, Quake axes cm) as an .xscn light. Every Costa light actor is bHidden
    (shown by the time-of-day and quest logic): the flag is not read."""
    p = rec.get('properties') or {}
    if p.get('bVisible') is False or p.get('bAffectsWorld') is False:
        return None
    spot = rec['componentClass'].endswith('SpotLightComponent')
    lc = p.get('LightColor') or {'R': 255, 'G': 255, 'B': 255}
    col = srgb_linear([lc['R'], lc['G'], lc['B']])
    if p.get('bUseTemperature'):
        col = col * kelvin_rgb(float(p.get('Temperature', 6500.0)))
    col = col / max(col.max(), 1e-6)
    inten = float(p.get('Intensity', 5000.0))
    units = str(p.get('IntensityUnits', 'ELightUnits::Unitless'))
    cd = inten / (4 * math.pi) if 'Lumens' in units else inten if 'Candelas' in units else inten / 625.0 * 100.0 / (4 * math.pi)
    radius = float(p.get('AttenuationRadius', 1000.0)) * UPCM
    pos = (M[:3, 3] - origin) * UPCM
    d = M[:3, 0] / (np.linalg.norm(M[:3, 0]) or 1.0)        # a UE light points along its local X
    # (Rebirth authors full cone angles, Inner/OuterFullConeAngle; stock UE the half angles)
    outer = math.radians(float(p['OuterFullConeAngle']) / 2 if 'OuterFullConeAngle' in p else
                         float(p.get('OuterConeAngle', 44.0))) if spot else math.pi
    inner = math.radians(float(p['InnerFullConeAngle']) / 2 if 'InnerFullConeAngle' in p else
                         float(p.get('InnerConeAngle', 0.0))) if spot else math.pi * 0.6
    shadows = bool(p.get('CastShadows', True)) and spot
    return (pos, radius, col, cd * scale, d, math.cos(outer), math.cos(inner), (1 if spot else 0) | (4 if shadows else 0),
            None, fog, (0.0, 0.0, 0.0, 0), (np.zeros(3), float(p.get('SourceRadius', 0.0)) * UPCM))


# ----------------------------------------------------------------------------------------------------- main

def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--extracted', required=True)
    ap.add_argument('--massive', required=True)
    ap.add_argument('--surround', help="the area's surround streams decoded (ff7rmassive.py): its folder (default: none)")
    ap.add_argument('--surround-extracted', help="the surround's extraction root (its Textures)")
    ap.add_argument('--farland-near', type=float, default=400.0,
                    help="metres: with --surround, the surround streams' instances stop past this eye distance and "
                         "the area's FarLand stand-ins (Static_Decoded_v2 under --surround-extracted) start (0: no "
                         "FarLand; every instance kept). Costa's beach view drew 10 of its 12 M triangles past 400 m")
    ap.add_argument('--farland-overlap', type=float, default=100.0,
                    help='metres both ways about --farland-near where both are drawn (each measured from its own '
                         'centre: no gap between a large strip piece and its tile)')
    ap.add_argument('--farland-tile', type=float, default=250.0, help='metres: the FarLand meshes cut into tiles '
                    '(the renderer measures an instance from its centre)')
    ap.add_argument('--surround-margin', type=float, default=1000.0, help='metres past the box: surround instances')
    ap.add_argument('--surround-grass-margin', type=float, default=150.0, help='metres past the box: surround grass')
    ap.add_argument('--blueprints', required=True, help='the prop Blueprints dump (inspect_blueprints.log)')
    ap.add_argument('--prop-maps', required=True, help='the prop maps dump (inspect_prop_maps.log)')
    ap.add_argument('--area', default='costa', choices=sorted(AREAS))
    ap.add_argument('--state', default='after', help="Jonny's hotel: before | after (the end game, as hide_states)")
    ap.add_argument('--time', default='day', help='the TimeSet layer: morning | day | sunset (its lights)')
    ap.add_argument('--hide-state', action='append',
                    help="an EnvironmentAccessor key prefix whose switched actors are left out (repeatable; default "
                         "the area's hide_states)")
    ap.add_argument('--map', default='ff7r_costa')
    ap.add_argument('--tag', type=int, required=True)
    ap.add_argument('--out', required=True, help='builds folder (the build goes in <out>/<map>_scene-<tag>)')
    ap.add_argument('--cache', required=True, help='image cache folder (DDS, shared by builds)')
    ap.add_argument('--tex-size', type=int, default=1024)
    ap.add_argument('--orm-size', type=int, default=512)
    ap.add_argument('--lod-density', type=float, default=0.5,
                    help='Massive LODs: at most this many triangles a pixel of the projected diameter squared (1080 '
                         'rows): a floor under the --lod-screen chain (0: the chain alone)')
    ap.add_argument('--lod-screen', type=float, default=0.3, help="LOD0's share of the view height; each LOD half")
    ap.add_argument('--light-scale', type=float, default=1.0)
    ap.add_argument('--light-fog', type=float, default=0.3)
    ap.add_argument('--emissive', type=float, default=4.0)
    ap.add_argument('--box', type=float, nargs=4, default=COSTA_BOX, help='cm, Quake axes: x0 y0 x1 y1')
    ap.add_argument('--origin', type=float, nargs=3, help='cm, Quake axes (default: the box centre, z 0)')
    ap.add_argument('--limit-streams', help='comma list: only these Massive streams (tests)')
    ap.add_argument('--no-sea', action='store_true', help='no sea surface (SEA_*)')
    ap.add_argument('--massive-normals', default='faces', choices=('faces', 'source'),
                    help="the Massive meshes' normals: smoothed from their faces (default) or the decoder's")
    ap.add_argument('--sea-reach', type=float, default=12000.0, help='metres: the sea out from the origin each way')
    a = ap.parse_args()
    t0 = time.time()
    area = AREAS[a.area]
    ex = Path(a.extracted)
    x0, y0, x1, y1 = a.box
    origin = np.array(a.origin if a.origin else [(x0 + x1) / 2, (y0 + y1) / 2, 0.0])
    inbox = lambda p: x0 <= p[0] <= x1 and y0 <= p[1] <= y1
    out = Path(a.out) / ('%s_scene-%d' % (a.map, a.tag))
    if out.exists():
        sys.exit('%s exists: builds are numbered, never overwritten' % out)
    out.mkdir(parents=True)
    tex = TexFinder([ex / 'Textures', ex / 'Materials', ex / 'Skeletal_Props', ex / 'Sky']
                    + ([Path(a.surround_extracted) / 'Textures', Path(a.surround_extracted) / 'Materials']
                       if a.surround_extracted else []))
    mats = Materials(a.map, tex, a.cache, a.tex_size, a.orm_size, a.emissive)
    mj = MaterialJson([ex / 'Materials', ex / 'Skeletal_Props']
                      + ([Path(a.surround_extracted) / 'Materials'] if a.surround_extracted else []))
    sc = Scene()
    stats = collections.Counter()

    # Massive streams
    def ring(m):            # inside the box grown by m metres
        g = m * 100.0
        return lambda p: x0 - g <= p[0] <= x1 + g and y0 - g <= p[1] <= y1 + g
    streams = [(s, Path(a.massive), inbox) for s in area['massive'] + area['massive_state'][a.state]]
    if a.surround:
        streams += [(s, Path(a.surround), ring(a.surround_grass_margin if 'Grass' in s else a.surround_margin))
                    for s in area.get('surround', ())]
    farland = bool(a.surround and a.surround_extracted and a.farland_near > 0 and area.get('farland'))
    far_hide = (a.farland_near + a.farland_overlap) * 100.0 * UPCM if farland else 0.0
    surround_stems = set(area.get('surround', ()))
    if a.limit_streams:
        streams = [t for t in streams if t[0] in a.limit_streams.split(',')]
    for stem, mdir, keep in streams:
        d = json.loads((mdir / (stem + '.json')).read_text(encoding='utf-8'))
        arr = np.load(mdir / (stem + '.npz'))
        P, N, U, IDX = arr['pos'], arr['nrm'], arr['uv'], arr['idx']
        local = {}
        for inst in d['instances']:
            p = np.array(inst['pos'])
            if not keep(p):
                stats['massive_outside_box'] += 1
                continue
            mi = inst['mesh']
            if mi not in local:
                m = d['meshes'][mi]
                mat = mats.add(('massive',) + tuple(m['textures']), massive_roles(m['textures']),
                               'opaque', '%s/%s' % (stem, m['name']))
                L0 = m['lods'][0]
                v0 = slice(L0['v0'], L0['v0'] + L0['vn'])
                mats.add_detail(mat, massive_tile(m['textures']),
                                uv_metres(P[v0], U[v0], IDX[L0['i0']:L0['i0'] + L0['in']].reshape(-1, 3).astype(np.int64)))
                lp = []
                for L in m['lods']:
                    v = slice(L['v0'], L['v0'] + L['vn'])
                    tris = IDX[L['i0']:L['i0'] + L['in']].reshape(-1, 3).astype(np.int64)
                    # (the decoded normals are not the surface's: the add-on's mec decoder leaves the packed tangent
                    # frame undecoded, and over all 3,223 Costa meshes a face and its vertices' normals agree at a
                    # median dot of -0.07 -- rocks shaded as crumpled foil. Smoothed from the faces instead, per vertex:
                    # the source's split vertices keep its hard edges; --massive-normals source keeps them)
                    lp.append([(P[v] * UPCM, N[v] if a.massive_normals == 'source' else None, U[v], tris, mat)])
                local[mi] = sc.add_mesh(('massive', stem, mi), lp, lod_screens(len(lp), a.lod_screen),
                                         density=a.lod_density)
            if local[mi] is None:
                continue
            W = np.eye(4)
            W[:3, :3] = quat_matrix(inst['quat'])
            W[:3, 3] = (p - origin) * UPCM
            sc.place(local[mi], W, hide=far_hide if stem in surround_stems else 0.0)
            stats['massive_instances'] += 1
        print('scene: %s %d instances (%.0f s)' % (stem, stats['massive_instances'], time.time() - t0), flush=True)

    # the surround's FarLand stand-ins, whole (they reach the horizon), cut into --farland-tile tiles, each drawn only
    # past --farland-near less the overlap
    if farland:
        far_near = max(a.farland_near - a.farland_overlap, 0.0) * 100.0 * UPCM
        tile = a.farland_tile * 100.0
        for stem in area['farland']:
            f = Path(a.surround_extracted) / 'Static_Decoded_v2' / (stem + '.gltf')
            if not f.exists():
                stats['farland_missing'] += 1
                continue
            doc = gltfio.Document(f)
            for k, (pos, nrm, uv, tris, _) in enumerate(doc.primitives(0)):
                prim = doc.doc['meshes'][0]['primitives'][k]
                mrec = doc.materials[prim['material']] if 'material' in prim else {}
                mpath = (mrec.get('extras') or {}).get('material') or mrec.get('name')
                roles, blend = mj.roles(mpath)
                if roles is None:
                    stats['material_json_missing'] += 1
                    roles = {}
                mat = mats.add(('json', mpath), roles, blend, mpath)
                cm = pos.astype(np.float64) * 100.0                     # (cm, Quake axes, about the game's origin)
                cell = np.floor(cm[tris].mean(1)[:, :2] / tile).astype(np.int64)
                for c in np.unique(cell, axis=0):
                    t = tris[(cell[:, 0] == c[0]) & (cell[:, 1] == c[1])]
                    used, t2 = np.unique(t, return_inverse=True)
                    mi = sc.add_mesh(('farland', stem, k, int(c[0]), int(c[1])),
                                     [[((cm[used] - origin) * UPCM, None if nrm is None else nrm[used],
                                        None if uv is None else uv[used], t2.reshape(-1, 3), mat)]], [0.0])
                    if mi is not None:
                        sc.place(mi, np.eye(4), near=far_near)
                        stats['farland_tiles'] += 1
                        stats['farland_tris'] += len(t)
        print('scene: FarLand %d tiles (%.0f s)' % (stats['farland_tiles'], time.time() - t0), flush=True)

    # placed actors: static meshes, Blueprint props, lights
    classes = blueprint_classes(a.prop_maps)
    bp = blueprint_parts(a.blueprints)
    static_dir, skel_dir = ex / 'Static_Decoded_v2', ex / 'Skeletal_Props'
    skel_files = {p.stem: p for p in skel_dir.rglob('*.gltf')}

    clip_geo, clip_tris = {}, []

    def place_clip(W):
        for pos, tris in clip_geo.get(static_mesh.key, ()):
            clip_tris.append((pos @ W[:3, :3].T + W[:3, 3])[tris])
            stats['clip_pieces'] += 1

    def static_mesh(path, overrides):
        name = path.rsplit('/', 1)[-1]
        key = ('static', name, tuple(overrides or ()))
        static_mesh.key = key
        if key in sc.mesh_index:
            return sc.mesh_index[key]
        f = static_dir / (name + '.gltf')
        if not f.exists():
            stats['static_missing'] += 1
            sc.mesh_index[key] = None
            return None
        doc = gltfio.Document(f)
        parts = []
        for k, (pos, nrm, uv, tris, _) in enumerate(doc.primitives(0)):
            prim = doc.doc['meshes'][0]['primitives'][k]
            mrec = doc.materials[prim['material']] if 'material' in prim else {}
            slot = (mrec.get('extras') or {}).get('slot_index', 0)
            mpath = (mrec.get('extras') or {}).get('material') or mrec.get('name')
            # (an override inside the map package, an inline instance the placements name by the map alone: the
            # mesh's own material)
            if overrides and slot < len(overrides) and overrides[slot] and '/Layout/' not in overrides[slot]:
                mpath = overrides[slot]
            if '/Environment/Collision/' in (mpath or ''):
                # the game's invisible blockers (M_CL_*: magenta in the editor): never drawn; the gameplay BSP
                # takes them as playerclip (clip.npz)
                clip_geo.setdefault(key, []).append((pos * 100.0 * UPCM, tris))
                continue
            roles, blend = mj.roles(mpath)
            if roles is None:
                stats['material_json_missing'] += 1
                roles = {}
            mat = mats.add(('json', mpath), roles, blend, mpath)
            parts.append((pos * 100.0 * UPCM, nrm, uv, tris, mat))
        return sc.add_mesh(key, [parts], [0.0])

    def skeletal_mesh(path):
        name = path.rsplit('/', 1)[-1]
        key = ('skeletal', name)
        if key in sc.mesh_index:
            return sc.mesh_index[key]
        f = skel_files.get(name)
        if f is None:
            stats['skeletal_missing'] += 1
            sc.mesh_index[key] = None
            return None
        doc = gltfio.Document(f)
        parts = []
        for pos, nrm, uv, tris, mname in doc.primitives(0):
            q = np.c_[pos[:, 0], -pos[:, 2], pos[:, 1]] * 100.0 * UPCM       # glTF Y-up metres -> Quake axes units
            qn = None if nrm is None else np.c_[nrm[:, 0], -nrm[:, 2], nrm[:, 1]]
            roles, blend = mj.roles(mname or '')
            if roles is None:
                stats['material_json_missing'] += 1
                roles = {}
            mat = mats.add(('json', mname), roles, blend, mname)
            parts.append((q, qn, uv, tris, mat))
        return sc.add_mesh(key, [parts], [0.0])

    lrecs = []
    placed = area['placed'] + area['placed_state'][a.state] + [area['timeset'] % a.time.capitalize()]
    hide = tuple(a.hide_state if a.hide_state is not None else area.get('hide_states', ()))
    for mapname in placed:
        recs = json.loads((ex / 'Placements' / (mapname + '.json')).read_text(encoding='utf-8'))
        # story-state props: an EnvironmentAccessor actor's EnvironmentMap names the actors the game shows or hides by
        # key; those under a hidden key's prefix are left out
        hidden = set()
        actor_of = lambda o: str(o.get('ObjectName') or '').rstrip("'").rsplit('.', 1)[-1] if isinstance(o, dict) else ''
        members = {r['actor']: [actor_of(t) for t in (r.get('actorProperties') or {}).get('TargetActors') or []]
                   for r in recs if (r.get('actorProperties') or {}).get('TargetActors')}

        kept = set()

        def mark(into, name, depth=0):
            if name and name not in into and depth < 8:
                into.add(name)
                for m in members.get(name, ()):            # a group actor: its members too
                    mark(into, m, depth + 1)
        for r in recs:
            em = (r.get('actorProperties') or {}).get('EnvironmentMap')
            for e in em if isinstance(em, list) else []:
                if isinstance(e, dict):
                    mark(hidden if state_key_hidden(str(e.get('Key', '')), hide) else kept, actor_of(e.get('Value')))
        hidden -= kept      # (an actor a kept key shows stays: a cutscene's key also names the boat's after state)
        if hidden:
            n0 = len(recs)
            recs = [r for r in recs if r['actor'] not in hidden]
            stats['state_hidden'] += n0 - len(recs)
        by_comp ={(r['actor'], r['component']): r for r in recs if r.get('component')}
        parents = attach_parents(ex / 'Inspect' / 'logs' / (mapname + '.log'))
        def world(r, depth=0):
            M = ue_to_quake(r['location_cm'], r['rotation_deg'], r['scale'])
            if r.get('attachParent') and depth < 8:
                pr = by_comp.get(parents.get((r['actor'], r.get('component'))))
                if pr is not None and pr is not r:
                    return world(pr, depth + 1) @ M
                stats['attach_parent_unresolved'] += 1
            return M
        for r in recs:
            cc = r.get('componentClass') or ''
            if cc.endswith('PointLightComponent') or cc.endswith('SpotLightComponent'):
                if any(s in r['actor'] for s in SKIP_LIGHTS):
                    stats['lights_skipped'] += 1
                    continue
                W = world(r)
                L = light_record(r, W, origin, a.light_scale, a.light_fog)
                if L is not None and inbox(W[:3, 3]):
                    lrecs.append(L)
                    stats['lights'] += 1
                continue
            W = world(r)
            if not inbox(W[:3, 3]) and 'FarLand' not in mapname:
                stats['placed_outside_box'] += 1
                continue
            W2 = W.copy()
            W2[:3, 3] = (W[:3, 3] - origin) * UPCM
            if r.get('staticMesh'):
                mi = static_mesh(r['staticMesh'], r.get('overrideMaterials'))
                place_clip(W2)
                if mi is not None:
                    sc.place(mi, W2)
                    stats['placed_static'] += 1
                continue
            cls = classes.get(r.get('actorPath'))
            if cls is None:
                continue
            for c in bp.get(cls, []):
                R = ue_to_quake(c['loc'], c['rot'], c['scale'])
                mi = static_mesh(c['mesh'], None) if c['kind'] == 'static' else skeletal_mesh(c['mesh'])
                if c['kind'] == 'static':
                    place_clip(W2 @ R)
                if mi is not None:
                    sc.place(mi, W2 @ R)
                    stats['placed_' + c['kind'] + '_bp'] += 1
        print('scene: %s placed (%.0f s)' % (mapname, time.time() - t0), flush=True)

    # the sea: Rebirth draws its ocean procedurally (M_Ocean_WORLD: a run-time surface, no mesh in any layer); the
    # level's own ocean layout (2110-COSTT_LevelPlan_Ocean) floats its drop boxes at z 0 cm, the sea level. Here a flat
    # grid at that level in the renderer's water mode (cp2gltf WATER_*: a blend flagged 0x80, a ripple normal map, the
    # tint the colour in the water), in tiles so the far ones cull; land stands over it by depth
    if not a.no_sea:
        from cp2gltf import WATER_FLAG, water_ripple_normal
        paths = ['', mats.images.add(water_ripple_normal(), 'n'),
                 mats.images.add(np.full((4, 4, 3), (255, round(255 * SEA_ROUGH), 0), np.uint8), 'o')]
        wi = len(mats.recs)
        mats.recs.append((paths, SCENE_KIND['blend'], WATER_FLAG, [1.0, 1.0, 1.0, 0.0],
                          [c / 255.0 for c in SEA_TINT] + [1.0], [0.0, 0.0]))
        mats.info.append({'index': wi, 'name': 'ff7r_sea', 'mesh': 'ff7r_sea', 'kind': 'water', 'roles': {}, 'images': paths})
        mats.index['ff7r_sea'] = wi
        cell = SEA_CELL_M * 100.0 * UPCM
        n = SEA_TILE_CELLS
        g = (np.arange(n + 1) - n / 2.0) * cell
        gx, gy = np.meshgrid(g, g)
        pos = np.stack([gx.ravel(), gy.ravel(), np.zeros(gx.size)], -1)
        q = np.arange(n * n)
        r0 = q // n * (n + 1) + q % n
        tris = np.concatenate([np.stack([r0, r0 + 1, r0 + n + 2], -1), np.stack([r0, r0 + n + 2, r0 + n + 1], -1)])
        nrm = np.tile([0.0, 0.0, 1.0], (len(pos), 1))
        tile = sc.add_mesh(('sea',), [[(pos, nrm, pos[:, :2] / (400.0 * UPCM), tris, wi)]], [0.0])
        span = n * cell
        k = int(math.ceil(a.sea_reach * 100.0 * UPCM / span))
        z = (SEA_Z_CM - origin[2]) * UPCM
        for i in range(-k, k + 1):
            for j in range(-k, k + 1):
                W = np.eye(4)
                W[:3, 3] = (i * span, j * span, z)
                sc.place(tile, W)
                stats['sea_tiles'] += 1

    # write
    V = np.concatenate(sc.V) if sc.V else np.zeros(0, SCENE_VERT)
    I = np.concatenate(sc.I) if sc.I else np.zeros(0, '<u4')
    blob = xscn_blob(V, I, sc.meshes, sc.lods, sc.parts, mats.recs, sc.inst, lrecs)
    xdet = mats.xdet()          # (before the flush: its share map is an image)
    print('scene: encoding %d images (%.0f s)' % (len(mats.images.todo), time.time() - t0), flush=True)
    new = mats.images.flush()
    mats.memo.save()
    sw = pk3write.ShardWriter(str(out), a.map)
    sw.first.writestr('maps/%s.xscn' % a.map, blob, compress_type=zipfile.ZIP_DEFLATED, compresslevel=1)  # (level 6: 19 s a 500 MB scene)
    if 'ff7r_sea' in mats.index:
        # maps/<map>.xwat (vk_scene_files.cpp Scene_LoadWater, version 1): the sea material's settings, no foam image
        import struct
        sw.first.writestr('maps/%s.xwat' % a.map, struct.pack('<4sIII', b'XWAT', 1, 1, 0)
                          + struct.pack('<I9f', mats.index['ff7r_sea'], *SEA_XWAT) + bytes(64))
    if xdet:
        sw.first.writestr('maps/%s.xdet' % a.map, xdet, compress_type=zipfile.ZIP_DEFLATED)
    for path in sorted(mats.images.todo):
        sw.stored(zipfile.ZipInfo(path), mats.images.cached(path).read_bytes())
    sw.close()
    (out / 'materials.json').write_text(json.dumps(mats.info, indent=1), encoding='utf-8')
    # each scene mesh's source (ff7rbuild.py picks collision by it): ['massive', stream, mesh] | ['static', name, ...]
    # | ['skeletal', name]
    names = [None] * len(sc.meshes)
    for key, mi in sc.mesh_index.items():
        if mi is not None:
            names[mi] = [str(k) for k in key]
    (out / 'meshes.json').write_text(json.dumps(names), encoding='utf-8')
    # the blockers' triangles, scene units (ff7rbuild.py: playerclip)
    np.savez_compressed(out / 'clip.npz', tris=np.concatenate(clip_tris).astype(np.float32) if clip_tris
                        else np.zeros((0, 3, 3), np.float32))
    # the drawn box (the area's, grown to the surround's reach when it is on) and the playable one (ff7rbuild's
    # collision and boundary: the area's own; the surround is scenery, and its render meshes as collision made
    # costa-9's compile 36 tiles, 53 GB, before it was stopped)
    g = a.surround_margin * 100.0 if a.surround else 0.0
    draw_box = [x0 - g, y0 - g, x1 + g, y1 + g]
    rec = {'map': a.map, 'area': a.area, 'state': a.state, 'time': a.time, 'origin_cm': origin.tolist(), 'box_cm': draw_box,
           'play_box_cm': [x0, y0, x1, y1],
           'units_per_cm': UPCM, 'meshes': len(sc.meshes), 'lods': len(sc.lods), 'parts': len(sc.parts),
           'instances': len(sc.inst), 'vertices': int(len(V)), 'triangles': int(len(I) // 3), 'lights': len(lrecs),
           'materials': len(mats.recs), 'images': len(mats.images.todo), 'images_encoded': new,
           'xscn_bytes': len(blob), 'stats': dict(stats), 'material_stats': dict(mats.stats),
           'packs': [os.path.basename(z.filename) for z in sw.packs], 'seconds': round(time.time() - t0)}
    (out / 'scene.json').write_text(json.dumps(rec, indent=1), encoding='utf-8')
    print('FF7R_SCENE ' + json.dumps({k: rec[k] for k in ('meshes', 'instances', 'triangles', 'lights', 'materials',
                                                          'images', 'xscn_bytes', 'packs', 'seconds')}))
    print(json.dumps(rec['stats']), json.dumps(rec['material_stats']))


if __name__ == '__main__':
    main()
