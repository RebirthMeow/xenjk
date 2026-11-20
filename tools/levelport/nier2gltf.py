"""Export NieR:Automata world tiles to a levelport glTF (.glb): render meshes, placed props, textures, collision,
spawn points.

    python tools/levelport/nier2gltf.py g11420 [g11419 ...] --out camp.glb [--variant before] [--texsize 1024]

Reads the game's archives directly (CPK -> DAT/DTT) with the PlatinumGames readers from the 2b-smallsword project
(--nier-tools, default $NIER_TOOLS: research/decoders for cpk/wmb, tools/texture.py for WTA/WTP). The game's data
folder (the one holding data003.cpk) is --game, default $NIER_DATA.

A world tile gXXXYY lives in wd1: the .dat holds the collision (.col, COL2), the prop layout (Layout.lay) and the
texture table (.wta); the .dtt holds the model (.wmb, WMB3) and the textures (.wtp). Textures a model does not
carry are in the shared world packs wda/ga000N. Props are separate models (bg/bgXXXX, ba/baXXXX) with their own
textures and collision.

Materials follow the game's per-material parameters (the WMB3 variable block), not their names:
- g_Tile (PBS00, Plt00, CNS00) is baked into the UVs.
- PBS10 (three-layer terrain blend): one material with all three albedo layers in extras "lp_layers" (texture index
  and g_MaterialTile tiling each) and the vertex colour as COLOR_0. Layer 2 is weighted by red, layer 3 by green.
- g_1BitMask: alpha-tested cutout (any texture format, so BC1 punch-through counts). Other alpha is data: opaque.
- g_Decal: alpha-blended decal. CNS00 g_UseMultiplicationBlend / g_UseSubtractionBlend: multiply decal.
- CNS00 without a decal flag is unlit; extras "lp_emissive" carries g_AlbedoColor x g_Intensity (lamps).

Roles written (README.md):
- render: LOD0 batches, one glTF mesh per mesh group; props are one mesh per model, a node (matrix) per placement.
- collision: COL2 meshes as levelport "shape: mesh" collision (gltf2map makes one thin brush per triangle). The
  type byte decides contents: 127 and 255 solid, 3 (tall invisible boundary walls) playerclip, 1 and 95 by
  --clip-types (default: solid). Meshes named for the other story variant are skipped. Props add theirs per placement.
- entities: info_player_deathmatch spawns on up-facing world collision, spread by farthest-point sampling.
Mesh groups and collision named camp_before/camp_after follow --variant.
"""
import argparse
import io
import math
import os
import struct
import sys
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import gltfio  # noqa: E402
import levelport as lp  # noqa: E402

GAME = Path(os.environ.get('NIER_DATA') or '.')    # (main sets it from --game)
CPK_DAT = ('data013.cpk', 'data003.cpk')
CPK_DTT = ('data014.cpk', 'data004.cpk')
SHARED = (('data013.cpk', 'data014.cpk', 'ga0000'), ('data003.cpk', 'data004.cpk', 'ga0001'),
          ('data003.cpk', 'data004.cpk', 'ga0002'))
PROP_DIRS = ('bg', 'ba', 'bh', 'bm')
COL_CONTENTS = {127: ['solid'], 255: ['solid'], 3: ['playerclip']}
COL_WATER = 88                 # "WCOL": the water surface's own collision plane; water volumes replace it
DUMMY_TEXTURE = 1318852340     # the id unused texture slots (g_LightMap, an absent g_MaskMap) point at


def load_tools(root):
    sys.path.insert(0, str(Path(root) / 'research' / 'decoders'))
    sys.path.insert(0, str(Path(root) / 'tools'))
    import cpk, eff, texture, wmb  # noqa: E401
    return cpk, eff, texture, wmb


class Archives:
    def __init__(self, cpk, eff):
        self.cpk, self.eff, self.index = cpk, eff, {}
        self.props = None

    def entry(self, cpk_name, dirn, name):
        if cpk_name not in self.index:
            self.index[cpk_name] = {(e['dir'], e['name']): e for e in self.cpk.list_files(str(GAME / cpk_name))}
        return self.index[cpk_name].get((dirn, name))

    def container(self, cpks, dirn, name):
        for c in cpks:
            e = self.entry(c, dirn, name)
            if e and e['extract_size']:
                blob = self.cpk.read_file(str(GAME / c), e)
                return {k: blob[o:o + s] for k, (o, s) in self.eff.dat_list(blob).items()}
        return None

    def prop(self, model):
        """(dat, dtt) containers of a prop model (bg/bgXXXX, ba/baXXXX, ...), from whichever CPK holds it."""
        if self.props is None:
            self.props = {}
            for f in sorted(os.listdir(GAME)):
                if f.endswith('.cpk'):
                    for e in self.cpk.list_files(str(GAME / f)):
                        if e['dir'] in PROP_DIRS and e['extract_size']:
                            self.props.setdefault(e['name'].lower(), (f, e))
        out = []
        for ext in ('.dat', '.dtt'):
            hit = self.props.get(model.lower() + ext)
            if not hit:
                return None
            blob = self.cpk.read_file(str(GAME / hit[0]), hit[1])
            out.append({k: blob[o:o + s] for k, (o, s) in self.eff.dat_list(blob).items()})
        return tuple(out)


# ---------------------------------------------------------------- WMB (map vertex layouts, material variables)

def material_vars(w):
    """[{variable name: float}] per material: the WMB3 material record's variable block (offset/count at words
    10/11 of the 0x30-byte record)."""
    d, out = w.data, []
    for i in range(len(w.materials())):
        raw = struct.unpack_from('<12I', d, w.header['materialArrayOffset'] + i * 0x30)
        vo, vn = raw[10], raw[11]
        v = {}
        for k in range(vn):
            no, val = struct.unpack_from('<If', d, vo + 8 * k)
            v[d[no:d.index(b'\0', no)].decode('ascii', 'replace')] = val
        out.append(v)
    return out


def vertex_group_arrays(w):
    """Whole vertex groups as numpy: positions, uv0, normals, the u32 index buffer and the vertex colour bytes.
    Map/prop layouts keep position (+0), packed tangent (+12), uv0 (+16, half2), uv1 (+20) and the colour (+24) in
    the 28-byte core record and a half4 normal at the start of the extra data."""
    d, out = w.data, []
    for vg in w.vertex_groups():
        n, st, ex = vg['vertexCount'], vg['vertexStride'], vg['vertexExDataStride']
        core = np.frombuffer(d, 'u1', n * st, vg['vertexArrayOffset']).reshape(n, st)
        exd = np.frombuffer(d, 'u1', n * ex, vg['vertexExDataArrayOffset']).reshape(n, ex)
        pos = core[:, 0:12].copy().view('<f4').astype(np.float64)
        uv = core[:, 16:20].copy().view('<f2').astype(np.float64)
        nrm = exd[:, 0:6].copy().view('<f2').astype(np.float64)
        col = core[:, 24:28].copy()                 # PBS10 blend weights: R layer 2, G layer 3
        faces = np.frombuffer(d, '<u4', vg['faceCount'], vg['faceArrayOffset'])
        out.append((pos, uv, nrm, faces, col))
    return out


def model_render(w, vgs, variant_skip=()):
    """-> {mesh group name: [(positions, normals, uv, colour bytes, tris, material index)]} for LOD0 (glTF
    counter-clockwise winding)."""
    meshes, groups = w.meshes(), w.mesh_groups()
    lods = w.lods()
    lod = next((l for l in lods if l['name'] == 'LOD0'), lods[0])
    out = {}
    for k, b in enumerate(lod['batches']):
        g = groups[b['meshGroupIndex']]['name']
        if g in variant_skip:
            continue
        m = meshes[lod['start'] + k]
        pos, uv, nrm, faces, col = vgs[m['vertexGroupIndex']]
        idx = faces[m['faceStart']:m['faceStart'] + m['faceCount']].astype(np.int64).reshape(-1, 3)
        used, inv = np.unique(idx, return_inverse=True)
        tris = inv.reshape(-1, 3)[:, [0, 2, 1]]        # stored clockwise; glTF wants counter-clockwise
        out.setdefault(g, []).append((pos[used], nrm[used], uv[used], col[used], tris, b['materialIndex']))
    return out


# ---------------------------------------------------------------- COL2

def read_col(data):
    """-> [(collisionType, modifier, surfaceType, name, triangles (n,3,3))]; triangles in stored winding."""
    magic, ver, ng_o, ng_n, m_o, m_n = struct.unpack_from('<4sIIIII', data, 0)
    if magic != b'COL2':
        raise ValueError('not COL2')
    names = [data[o:data.index(b'\0', o)].decode('ascii', 'replace') for o in struct.unpack_from('<%dI' % ng_n, data, ng_o)]
    out = []
    for i in range(m_n):
        ct, mod, unk, surf, name, btype, b_o, b_n = struct.unpack_from('<4B4I', data, m_o + i * 20)
        tris = []
        for j in range(b_n):
            if btype != 2:
                raise NotImplementedError('COL2 batch type %d (skinned collision)' % btype)
            bone, v_o, v_n, i_o, i_n = struct.unpack_from('<i4I', data, b_o + j * 20)
            v = np.frombuffer(data, '<f4', v_n * 4, v_o).reshape(v_n, 4)[:, :3].astype(np.float64)
            idx = np.frombuffer(data, '<u2', i_n, i_o).reshape(-1, 3)
            tris.append(v[idx])
        if tris:
            out.append((ct, mod, surf, names[name] if name < len(names) else str(name), np.concatenate(tris)))
    return out


# ---------------------------------------------------------------- layout (props)

def read_layout(data):
    """Layout.lay (LAY v2.01) -> [(instance name, model name, position, euler xyz radians, scale)].
    Header: magic, version, model list (offset, count; 4-byte ids), instances (offset, count; 0x70-byte records:
    name[32], position, rotation, scale, ..., child count at +0x6C), child transforms (offset, count; 36 bytes:
    position, rotation, scale, in world space). A record with children places its model at its own transform and
    at each child's, in order."""
    magic, ver, a_o, a_n, i_o, i_n, c_o, c_n = struct.unpack_from('<4sf6I', data, 0)
    if magic != b'LAY\0':
        raise ValueError('not a LAY layout')
    out, child = [], 0
    for k in range(i_n):
        o = i_o + 0x70 * k
        name = data[o:o + 32].split(b'\0')[0].decode('ascii', 'replace')
        model = name.split('_')[0]
        t = struct.unpack_from('<9f', data, o + 32)
        out.append((name, model, np.array(t[0:3]), np.array(t[3:6]), np.array(t[6:9])))
        for j in range(struct.unpack_from('<i', data, o + 0x6C)[0]):
            t = struct.unpack_from('<9f', data, c_o + 36 * (child + j))
            out.append(('%s.%d' % (name, j), model, np.array(t[0:3]), np.array(t[3:6]), np.array(t[6:9])))
        child += struct.unpack_from('<i', data, o + 0x6C)[0]
    if child != c_n:
        print('  warning: layout child count %d != table %d' % (child, c_n))
    return out


def placement_matrix(pos, rot, scale):
    """NieR placement -> 4x4 (NieR space = glTF space). Rotation is Ry . Rz . Rx (NieR2Blender's Euler XYZ with
    its axis swap undone)."""
    def r(axis, a):
        c, s = math.cos(a), math.sin(a)
        i, j = [(1, 2), (2, 0), (0, 1)][axis]
        m = np.eye(3)
        m[i, i], m[i, j], m[j, i], m[j, j] = c, -s, s, c
        return m
    m = np.eye(4)
    m[:3, :3] = r(1, rot[1]) @ r(2, rot[2]) @ r(0, rot[0]) @ np.diag(scale)
    m[:3, 3] = pos
    return m


WATER_RIPPLE_M = 6.0
WATER_GRID_M = 3.0     # vertex spacing of rebuilt flat water: the shader's gentle waves deform vertices


def water_quad(pos, uv, tris, region):
    """A flat water primitive -> an up-facing grid (WATER_GRID_M cells) over its extent cut to region, one UV unit
    per WATER_RIPPLE_M metres; None if nothing is left inside."""
    used = np.unique(tris)
    p = pos[used]
    lo = np.maximum(p[:, [0, 2]].min(0), region[0])
    hi = np.minimum(p[:, [0, 2]].max(0), region[1])
    if np.any(hi - lo < 0.5):
        return None
    y = float(p[:, 1].mean())
    nx, nz = (np.maximum(1, np.ceil((hi - lo) / WATER_GRID_M))).astype(int)
    gx, gz = np.meshgrid(np.linspace(lo[0], hi[0], nx + 1), np.linspace(lo[1], hi[1], nz + 1), indexing='ij')
    xz = np.c_[gx.ravel(), gz.ravel()]
    q = np.c_[xz[:, 0], np.full(len(xz), y), xz[:, 1]]
    # world-scaled ripples: the sea's own UVs put one ripple tile per 50 m (it is built for a 10 km view), which
    # reads as flat paint at player scale
    quv = xz / WATER_RIPPLE_M
    nrm = np.tile([0.0, 1.0, 0.0], (len(xz), 1))
    i = np.arange(nx)[:, None] * (nz + 1) + np.arange(nz)[None, :]
    a, b, c, d = i.ravel(), i.ravel() + nz + 1, i.ravel() + nz + 2, i.ravel() + 1     # (x,z) (x+1,z) (x+1,z+1) (x,z+1)
    # (c - a) x (b - a) and (d - a) x (c - a) point +Y: the triangles face up
    idx = np.r_[np.c_[a, c, b], np.c_[a, d, c]]
    return q, nrm, quv, np.zeros((len(xz), 4), np.uint8), idx


# ---------------------------------------------------------------- spawns

def headroom(points, tris, low=0.5, high=4.0):
    """True where no triangle of `tris` passes over the point between low and high metres above it (a nook under
    a table, stairs or an interior ceiling)."""
    ok = np.ones(len(points), bool)
    a, b, c = tris[:, 0][:, [0, 2]], tris[:, 1][:, [0, 2]], tris[:, 2][:, [0, 2]]
    lo, hi = np.minimum(np.minimum(a, b), c), np.maximum(np.maximum(a, b), c)
    for i, p in enumerate(points):
        q = p[[0, 2]]
        sel = np.all((lo <= q) & (q <= hi), axis=1)
        if not np.any(sel):
            continue
        A, B, C, T = a[sel], b[sel], c[sel], tris[sel]
        d = (B[:, 1] - C[:, 1]) * (A[:, 0] - C[:, 0]) + (C[:, 0] - B[:, 0]) * (A[:, 1] - C[:, 1])
        good = np.abs(d) > 1e-9
        d = np.where(good, d, 1.0)
        w0 = ((B[:, 1] - C[:, 1]) * (q[0] - C[:, 0]) + (C[:, 0] - B[:, 0]) * (q[1] - C[:, 1])) / d
        w1 = ((C[:, 1] - A[:, 1]) * (q[0] - C[:, 0]) + (A[:, 0] - C[:, 0]) * (q[1] - C[:, 1])) / d
        w2 = 1 - w0 - w1
        inside = good & (w0 >= 0) & (w1 >= 0) & (w2 >= 0)
        y = w0 * T[:, 0, 1] + w1 * T[:, 1, 1] + w2 * T[:, 2, 1]
        ok[i] = not np.any(inside & (y > p[1] + low) & (y < p[1] + high))
    return ok


def spawn_points(ground, n, margin=1.0, blockers=None, water=None):
    """Farthest-point sample n spawn positions on up-facing ground triangles (game space, metres); with blockers
    (all collision triangles), only where there is headroom."""
    T = ground
    nrm = np.cross(T[:, 2] - T[:, 0], T[:, 1] - T[:, 0])     # stored clockwise: this order points out
    area = np.linalg.norm(nrm, axis=1)
    up = (nrm[:, 1] / np.maximum(area, 1e-9) > 0.9) & (area > 0.5)
    cand = T[up].mean(1)
    if blockers is not None and len(cand):
        cand = cand[headroom(cand, blockers)]
    for lo, hi, y in water or ():          # not on the sea floor of a flooded street
        under = np.all((cand[:, [0, 2]] >= lo) & (cand[:, [0, 2]] <= hi), axis=1) & (cand[:, 1] < y + 0.3)
        cand = cand[~under]
    if not len(cand):
        return []
    centre = np.median(cand, axis=0)
    order = [int(np.argmin(np.linalg.norm(cand - centre, axis=1)))]
    while len(order) < min(n, len(cand)):
        d = np.min(np.linalg.norm(cand[:, None] - cand[order][None], axis=2), axis=1)
        order.append(int(np.argmax(d)))
    return [cand[i] + np.array([0, margin, 0]) for i in order]


def probe_points(water, blockers, step=50.0, height=3.0, limit=48):
    """Reflection probe positions (rd-vulkan misc_cubemap) on a step-metre grid over each water surface, height
    metres up, where open water reaches from below the surface to 10 m above it (not in a building or under a
    bridge). rd-vulkan then places no probes of its own, so the spawns get probes too (spawn_probes)."""
    pts = []
    for lo, hi, y in water or ():
        n = np.maximum(1, np.round((hi - lo) / step)).astype(int)
        for i in range(n[0]):
            for j in range(n[1]):
                xz = lo + (np.array([i, j]) + 0.5) * (hi - lo) / n
                pts.append([xz[0], y + height, xz[1]])
    pts = np.array(pts).reshape(-1, 3)
    if blockers is not None and len(pts):
        pts = pts[headroom(pts - np.array([0, height, 0]), blockers, low=-1.0, high=10.0)]
    return pts[:limit]


# ---------------------------------------------------------------- export

class Exporter:
    def __init__(self, a, arc, texture, wmb):
        self.a, self.arc, self.texture, self.wmb = a, arc, texture, wmb
        self.w = gltfio.Writer()
        self.shared = None
        self.blockers = []          # solid collision triangles in world space (spawn headroom)
        self.water_surfaces = []    # (xz min, xz max, height) of flat water, for water volumes
        self.water_levels = []      # the same per water volume, kept for spawn placement
        self.images = {}            # (tid, keep alpha) -> (bytes, mime, has alpha) or None
        self.stats = {'tris': 0, 'materials': 0, 'textures': 0, 'missing_textures': 0, 'col_tris': 0,
                      'props': 0, 'prop_models': 0, 'missing_props': 0, 'cutout': 0, 'decal': 0, 'layered': 0,
                      'emissive': 0}

    def dds(self, tid, local):
        if tid is None:
            return None
        if tid in local:
            return local[tid]
        if self.shared is None:
            self.shared = {}
            for c_dat, c_dtt, nm in SHARED:
                sd = self.arc.container((c_dat,), 'wda', nm + '.dat')
                st = self.arc.container((c_dtt,), 'wda', nm + '.dtt')
                if sd and st:
                    self.shared.update(self.texture.by_id(sd[nm + '.wta'], st[nm + '.wtp']))
        return self.shared.get(tid)

    def image(self, tid, local, keep_alpha):
        """-> (encoded bytes, mime, has alpha) or None. Alpha is kept (PNG) only when asked and present."""
        key = (tid, keep_alpha)
        if key not in self.images:
            dds = self.dds(tid, local)
            if dds is None:
                self.images[key] = None
                self.stats['missing_textures'] += tid is not None
            else:
                from PIL import Image
                im = Image.open(io.BytesIO(dds))
                im.load()
                im.thumbnail((self.a.texsize, self.a.texsize))
                rgba = im.convert('RGBA')
                has_alpha = int(np.asarray(rgba)[..., 3].min()) < 128
                buf = io.BytesIO()
                if keep_alpha and has_alpha:
                    rgba.save(buf, 'PNG')
                    self.images[key] = (buf.getvalue(), 'image/png', True)
                else:
                    rgba.convert('RGB').save(buf, 'JPEG', quality=92)
                    self.images[key] = (buf.getvalue(), 'image/jpeg', has_alpha)
                self.stats['textures'] += 1
        return self.images[key]

    def decoded(self, tid, local):
        """RGBA uint8 array of a texture at --texsize, or None."""
        dds = self.dds(tid, local)
        if dds is None or tid == DUMMY_TEXTURE:
            return None
        from PIL import Image
        im = Image.open(io.BytesIO(dds))
        im.load()
        im.thumbnail((self.a.texsize, self.a.texsize))
        return np.asarray(im.convert('RGBA'))

    def normal_map(self, tid, local):
        """glTF texture index of a NieR normal map converted to the OpenGL convention (green flipped: NieR's maps
        are DirectX, measured against their own AO), or None."""
        key = ('n', tid)
        if key not in self.images:
            px = self.decoded(tid, local)
            self.images[key] = None
            if px is not None:
                from PIL import Image
                rgb = px[..., :3].copy()
                rgb[..., 1] = 255 - rgb[..., 1]
                buf = io.BytesIO()
                Image.fromarray(rgb).save(buf, 'JPEG', quality=95)
                self.images[key] = self.w.texture(buf.getvalue(), 'image/jpeg')
                self.stats['normal_maps'] = self.stats.get('normal_maps', 0) + 1
        return self.images[key]

    def orm_map(self, tid, local, gloss, metal):
        """glTF texture index of an ORM map (R occlusion, G roughness, B metal) from a NieR mask (R metal,
        G glossiness, B occlusion). gloss/metal: scalar overrides (the material's g_b*OverWrite flags) or None.
        Without a mask the map is a 4x4 constant."""
        key = ('orm', tid, gloss, metal)
        if key not in self.images:
            from PIL import Image
            px = self.decoded(tid, local) if tid is not None else None
            if px is None:
                px = np.full((4, 4, 4), 255, np.uint8)
                px[..., 0], px[..., 1] = 0, round(255 * (gloss if gloss is not None else 0.2))
                if metal is None:
                    metal = 0.0
            orm = np.empty(px.shape[:2] + (3,), np.uint8)
            orm[..., 0] = px[..., 2]
            orm[..., 1] = 255 - (px[..., 1] if gloss is None else round(255 * gloss))
            orm[..., 2] = px[..., 0] if metal is None else round(255 * metal)
            buf = io.BytesIO()
            Image.fromarray(orm).save(buf, 'JPEG', quality=95)
            self.images[key] = self.w.texture(buf.getvalue(), 'image/jpeg')
        return self.images[key]

    def pbr_maps(self, m, v, local, suffix=''):
        """(normal texture, orm texture) for a NieR material or one layer of it (suffix '1'..'3')."""
        t = m['textures']
        normal = None
        if v.get('g_UseNormalMap', 1) > 0.5 and t.get('g_NormalMap' + suffix) is not None:
            normal = self.normal_map(t['g_NormalMap' + suffix], local)
        over_g = v.get('g_bGlossinessOverWrite', 0) > 0.5 or suffix
        over_m = v.get('g_bMetalicOverWrite', 0) > 0.5 or suffix
        gloss = v.get('g_Glossiness' + suffix, 0.2) if over_g else None
        metal = v.get('g_Metallic' + suffix, 0.0) if over_m else None
        orm = self.orm_map(None if suffix else t.get('g_MaskMap'), local, gloss, metal)
        return normal, orm

    def ripple_map(self, tid, local):
        """glTF texture index of a grey ripple pattern shaded from a water normal map (lit from up-left), or None."""
        key = ('ripple', tid)
        if key not in self.images:
            px = self.decoded(tid, local)
            self.images[key] = None
            if px is not None:
                from PIL import Image
                n = px[..., :3].astype(np.float64) / 127.5 - 1.0
                shade = np.clip(0.5 + 0.9 * (0.55 * n[..., 0] - 0.55 * n[..., 1]) + 0.25 * (n[..., 2] - 1), 0, 1)
                buf = io.BytesIO()
                Image.fromarray(np.round(shade * 255).astype(np.uint8)).convert('RGB').save(buf, 'JPEG', quality=92)
                self.images[key] = self.w.texture(buf.getvalue(), 'image/jpeg')
        return self.images[key]

    def water(self, name, m, v, local, flat=False):
        """Wtr00/Wtr02 (pools, sea): no albedo; colour, scrolling normal maps and fog. -> material extras lp_water
        (tint, opacity, ripple texture with its tiling and scroll)."""
        t = m['textures']
        col = [v.get('g_AlbedoColor_%s' % c, 0.1) for c in 'XYZ']
        fog = [v.get('g_CamToSurfaceFogColor_%s' % c, 0.1) for c in 'XYZ']
        # NieR draws the water over a refraction fogged towards g_SurfaceToBottomFogColor. With no scene depth in
        # the renderer it becomes a nearly opaque surface of that deep colour, used as is (its albedo, or the albedo
        # gamma-encoded, read as a lit green lawn under q3map2's vertex light):
        # what the eye reads as water is then the probe reflection on top
        color = [v.get('g_SurfaceToBottomFogColor_%s' % c, 0.03) for c in 'XYZ']
        tint = [min(1.0, 0.5 * (a + b) * 2.5 + 0.05) for a, b in zip(col, fog)]
        # flat water is rebuilt in world units (water_quad); small sloped pools keep their UVs and NieR's tiling
        tile = [1.0, 1.0] if flat else [v.get('g_NormalTile1_X', v.get('g_MaterialTile1_X', 1.0)) or 1.0,
                                       v.get('g_NormalTile1_Y', v.get('g_MaterialTile1_Y', 1.0)) or 1.0]
        scroll = [v.get('g_Normal_Animation1_X', 20.0) / 1000.0, v.get('g_Normal_Animation1_Y', 20.0) / 1000.0]
        nid = t.get('g_NormalMap1', t.get('g_NormalMap'))
        # the second normal layer's tiling relative to the first -> rd-vulkan's detail normal
        detail = (v.get('g_NormalTile2_X', 0) or 0) / (v.get('g_NormalTile1_X', 1) or 1)
        gloss = v.get('g_Glossiness1', 0.95)
        self.stats['water'] = self.stats.get('water', 0) + 1
        return self.w.material(name + ('_flat' if flat else ''), extras={'nier_shader': m['shader'], 'lp_water': {
            'tint': tint, 'opacity': 0.92, 'color': color, 'normal': self.normal_map(nid, local),
            'ripple': self.ripple_map(nid, local), 'tile': tile, 'scroll': scroll,
            'detail': [detail if detail > 1.05 else 2.3, 0.6], 'roughness': min(0.3, max(0.05, 1 - gloss)),
            # gentle waves only on rebuilt flat water (its grid has the vertices): amplitude m, wavelength m, period s
            'waves': [[0.07, 9.0, 4.0], [0.03, 3.5, 2.2]] if flat else None}})

    def material(self, prefix, m, v, local, flat_water=False):
        """One NieR material -> glTF material index and the UV tiling to bake (None for layered materials).
        flat_water: the primitive was rebuilt by water_quad (world-unit UVs)."""
        shader = m['shader']
        name = '%s_%s' % (prefix, m['name'])
        extras = {'nier_shader': shader}
        tile = (v.get('g_Tile_X', 1.0) or 1.0, v.get('g_Tile_Y', 1.0) or 1.0)
        if shader.startswith(('Wtr00', 'Wtr02')):
            return self.water(name, m, v, local, flat_water), (1.0, 1.0)
        if shader.startswith('Wtr01'):      # foam: waterfalls and splashes, additive and scrolling
            img = self.image(m['textures'].get('g_AlbedoMap1'), local, False)
            extras.update({'lp_blend': 'add', 'lp_scroll': [v.get('g_UvAnimation1_X', 0.0) / 10.0,
                                                            v.get('g_UvAnimation1_Y', 0.0) / 10.0],
                           'lp_add_scale': 0.3})     # NieR masks foam by a gradation map; full white blows out
            self.stats['decal'] += 1
            self.stats['materials'] += 1
            return self.w.material(name, image=img and img[0], mime=img and img[1], alpha='BLEND', extras=extras), \
                (v.get('g_MaterialTile1_X', 1.0) or 1.0, v.get('g_MaterialTile1_Y', 1.0) or 1.0)
        if shader.startswith(('PBS10', 'PBS11')):
            layers, base = [], None
            for k in (1, 2, 3):
                tid = m['textures'].get('g_AlbedoMap%d' % k)
                img = self.image(tid, local, False)
                if img is None:
                    continue
                tex = self.w.texture(img[0], img[1])
                base = base or img
                normal, orm = self.pbr_maps(m, v, local, str(k))
                layers.append({'texture': tex, 'tile': [v.get('g_MaterialTile%d_X' % k, 1.0),
                                                        v.get('g_MaterialTile%d_Y' % k, 1.0)],
                               'weight': [None, 'r', 'g'][k - 1], 'normal': normal, 'orm': orm})
            extras.update({'albedo_ids': [m['textures'].get('g_AlbedoMap%d' % k) for k in (1, 2, 3)],
                           'lp_layers': layers})
            self.stats['layered'] += 1
            self.stats['materials'] += 1
            return self.w.material(name, image=base and base[0], mime=base and base[1], extras=extras), None
        tid = m['textures'].get('g_AlbedoMap')
        extras['albedo_id'] = tid
        decal = v.get('g_Decal', 0) > 0.5
        mult = shader.startswith('CNS') and (v.get('g_UseMultiplicationBlend', 0) > 0.5 or
                                             v.get('g_UseSubtractionBlend', 0) > 0.5)
        cutout = v.get('g_1BitMask', 0) > 0.5
        img = self.image(tid, local, decal or cutout)
        alpha = None
        if mult:
            extras['lp_blend'] = 'multiply'
            alpha = 'BLEND'
            self.stats['decal'] += 1
        elif decal and img and img[2]:
            extras['lp_blend'] = 'alpha'
            alpha = 'BLEND'
            self.stats['decal'] += 1
        elif cutout and img and img[2]:
            alpha = 'MASK'
            self.stats['cutout'] += 1
        elif shader.startswith('CNS'):
            col = [v.get('g_AlbedoColor_%s' % c, 1.0) for c in 'XYZ']
            extras['lp_emissive'] = {'color': col, 'intensity': v.get('g_Intensity', 1.0),
                                     'tint': v.get('g_bAlbedoOverWrite', 0) > 0.5 or img is None}
            self.stats['emissive'] += 1
        self.stats['materials'] += 1
        normal = orm = None
        if img and alpha != 'BLEND' and not shader.startswith('CNS'):
            normal, orm = self.pbr_maps(m, v, local)
        mi = self.w.material(name, image=img and img[0], mime=img and img[1], alpha=alpha,
                             double_sided=alpha == 'MASK' or shader.startswith('Plt'), extras=extras,
                             normal=normal, orm=orm)
        return mi, tile

    def primitives(self, prefix, w, parts, local, mvars, region=None, water=True):
        """region: (xz min, xz max) to keep, dropping far scenery such as a 10 km sea plane: a triangle survives only
        with all three vertices inside. Flat water (Wtr00/Wtr02, height spread under 5 cm) is rebuilt as one quad
        cut to the region (its UVs from a planar fit of the source mesh), so 100 m sea triangles neither stick out
        nor leave a ragged edge, and (water: a tile's own, in world space; not a prop model's, whose positions are
        its own and not placed) is remembered for its water volume."""
        mats = w.materials()
        cache, prims = {}, []
        for pos, nrm, uv, col, tris, mi in parts:
            flat_water = mats[mi]['shader'].startswith(('Wtr00', 'Wtr02')) and np.ptp(pos[tris][..., 1]) < 0.05
            if flat_water and region is not None:
                got = water_quad(pos, uv, tris, region)
                if got is None:
                    continue
                pos, nrm, uv, col, tris = got
            elif region is not None:
                inside = np.all((pos[:, [0, 2]] >= region[0]) & (pos[:, [0, 2]] <= region[1]), axis=1)
                keep = inside[tris].all(1)
                if not keep.all():
                    self.stats['clipped_tris'] = self.stats.get('clipped_tris', 0) + int((~keep).sum())
                    tris = tris[keep]
                    if not len(tris):
                        continue
                    used, inv = np.unique(tris, return_inverse=True)
                    pos, nrm, uv, col, tris = pos[used], nrm[used], uv[used], col[used], inv.reshape(-1, 3)
            rebuilt = flat_water and region is not None
            if (mi, rebuilt) not in cache:
                cache[(mi, rebuilt)] = self.material(prefix, mats[mi], mvars[mi], local, rebuilt)
            gm, tile = cache[(mi, rebuilt)]
            if flat_water and water:
                self.water_surfaces.append((pos[:, [0, 2]].min(0), pos[:, [0, 2]].max(0), float(pos[0, 1])))
            prims.append({'position': pos, 'normal': nrm, 'uv': uv * np.array(tile) if tile else uv,
                          'color': col if tile is None else None, 'indices': tris, 'material': gm})
            self.stats['tris'] += len(tris)
        return prims

    def collision(self, parent, label, col, skip_names=(), matrix=None, ground=None):
        clip = {int(x) for x in self.a.clip_types.split(',') if x}
        skip = {int(x) for x in self.a.skip_types.split(',') if x}
        by_kind = {}
        for ct, mod, surf, name, T in col:
            if name in skip_names or ct in skip or ct == COL_WATER:
                continue
            contents = ['playerclip'] if (ct in clip or COL_CONTENTS.get(ct) == ['playerclip']) else ['solid']
            by_kind.setdefault((ct, tuple(contents)), []).append(T)
            if ct == 127 and ground is not None:
                ground.append(T)
            if contents == ['solid']:
                self.blockers.append(T if matrix is None else T @ matrix[:3, :3].T + matrix[:3, 3])
        for (ct, contents), Ts in sorted(by_kind.items()):
            T = np.concatenate(Ts)
            pos = T.reshape(-1, 3)
            tris = np.arange(len(pos)).reshape(-1, 3)[:, [0, 2, 1]]
            mi = self.w.material('collision_%s' % '_'.join(contents), extras={'surfaceparms': []})
            mesh = self.w.mesh('%s_col%d' % (label, ct), [{'position': pos, 'indices': tris, 'material': mi}])
            self.w.node('%s_col%d' % (label, ct), mesh=mesh, matrix=matrix, parent=parent,
                        extras={'lp': lp.ROLE_COLLISION, 'shape': 'mesh', 'contents': list(contents), 'nier_type': ct})
            self.stats['col_tris'] += len(T)
        return by_kind

    def water_volumes(self, parent, label, bottom):
        """One water-contents box under each height of flat water: the union of its surfaces' extents, down to the
        tile's lowest collision (or 30 m) so players swim instead of standing on the surface."""
        by_h = {}
        for lo, hi, y in self.water_surfaces:
            k = round(y, 1)
            a, b = by_h.get(k, (lo, hi))
            by_h[k] = (np.minimum(a, lo), np.maximum(b, hi))
        self.water_levels += [(lo, hi, y) for y, (lo, hi) in by_h.items()]
        self.water_surfaces = []
        corners = np.array([[i, j, k] for i in (0, 1) for j in (0, 1) for k in (0, 1)], float)
        faces = np.array([[0, 1, 3], [0, 3, 2], [4, 6, 7], [4, 7, 5], [0, 4, 5], [0, 5, 1],
                          [2, 3, 7], [2, 7, 6], [0, 2, 6], [0, 6, 4], [1, 5, 7], [1, 7, 3]])
        mi = self.w.material('collision_water', extras={'surfaceparms': []})
        for n, (y, (lo, hi)) in enumerate(sorted(by_h.items())):
            low = min(bottom - 1.0, y - 2.0) if bottom is not None else y - 30.0
            lo3, hi3 = np.array([lo[0], low, lo[1]]), np.array([hi[0], y, hi[1]])
            pos = lo3 + corners * (hi3 - lo3)
            mesh = self.w.mesh('%s_water%d' % (label, n), [{'position': pos, 'indices': faces, 'material': mi}])
            self.w.node('%s_water%d' % (label, n), mesh=mesh, parent=parent,
                        extras={'lp': lp.ROLE_COLLISION, 'contents': ['water', 'trans']})
            self.stats['water_volumes'] = self.stats.get('water_volumes', 0) + 1

    def tile(self, tile, g_render, g_col, g_props, ground):
        a, arc = self.a, self.arc
        other = 'camp_after' if a.variant == 'before' else 'camp_before'
        dat = arc.container(CPK_DAT, 'wd1', tile + '.dat')
        dtt = arc.container(CPK_DTT, 'wd1', tile + '.dtt')
        if not dat or not dtt:
            sys.exit('%s: tile not found in wd1' % tile)
        w = self.wmb.WMB(dtt[tile + '.wmb'])
        local = self.texture.by_id(dat[tile + '.wta'], dtt[tile + '.wtp']) if tile + '.wta' in dat else {}
        mvars = material_vars(w)
        col = read_col(dat[tile + '.col']) if tile + '.col' in dat else []
        region = None
        if col and a.margin >= 0:
            xz = np.concatenate([T.reshape(-1, 3)[:, [0, 2]] for ct, *_, T in col if ct != COL_WATER])
            region = (xz.min(0) - a.margin, xz.max(0) + a.margin)
        bottom = min(T[..., 1].min() for *_, T in col) if col else None
        for gname, parts in model_render(w, vertex_group_arrays(w), {other}).items():
            prims = self.primitives(tile, w, parts, local, mvars, region)
            if prims:
                self.w.node('%s_%s' % (tile, gname), mesh=self.w.mesh(gname, prims), extras={'lp': lp.ROLE_RENDER},
                            parent=g_render)
        if col:
            self.collision(g_col, tile, col, skip_names={other}, ground=ground)
        self.water_volumes(g_col, tile, bottom)
        if a.props and 'Layout.lay' in dat:
            self.props(read_layout(dat['Layout.lay']), g_props, g_col, region)

    def props(self, placements, g_props, g_col, region=None):
        """Placed props; with region, a placement whose bounds leave it is dropped."""
        models = {}
        for inst, model, pos, rot, scale in placements:
            if model not in models:
                models[model] = None
                got = self.arc.prop(model)
                if got is None:
                    self.stats['missing_props'] += 1
                    print('  warning: prop model %s not found' % model)
                    continue
                dat, dtt = got
                if model + '.wmb' not in dtt:
                    self.stats['missing_props'] += 1
                    continue
                w = self.wmb.WMB(dtt[model + '.wmb'])
                local = self.texture.by_id(dat[model + '.wta'], dtt[model + '.wtp']) if model + '.wta' in dat else {}
                prims = []
                mvars = material_vars(w)
                for gname, parts in model_render(w, vertex_group_arrays(w)).items():
                    prims += self.primitives(model, w, parts, local, mvars, water=False)
                if not prims:
                    continue
                col = read_col(dat[model + '.col']) if model + '.col' in dat else []
                pts = np.concatenate([p['position'] for p in prims])
                models[model] = (self.w.mesh(model, prims), col, pts.min(0), pts.max(0))
                self.stats['prop_models'] += 1
            if models[model] is None:
                continue
            mesh, col, blo, bhi = models[model]
            m = placement_matrix(pos, rot, scale)
            if region is not None:      # helper volumes (bga100: a 427 m sea box) and far set dressing
                corners = np.array([[x, y, z] for x in (blo[0], bhi[0]) for y in (blo[1], bhi[1])
                                    for z in (blo[2], bhi[2])]) @ m[:3, :3].T + m[:3, 3]
                if np.any(corners[:, [0, 2]] < region[0]) or np.any(corners[:, [0, 2]] > region[1]):
                    self.stats['props_outside'] = self.stats.get('props_outside', 0) + 1
                    continue
            self.w.node(inst, mesh=mesh, matrix=m, parent=g_props,
                        extras={'lp': lp.ROLE_RENDER, 'nier_model': model})
            if col:
                self.collision(g_col, inst, col, matrix=m)
            self.stats['props'] += 1


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('tiles', nargs='+')
    ap.add_argument('--out', required=True)
    ap.add_argument('--variant', default='before', choices=('before', 'after'))
    ap.add_argument('--texsize', type=int, default=1024)
    ap.add_argument('--spawns', type=int, default=8)
    ap.add_argument('--no-props', dest='props', action='store_false', help='skip the Layout.lay placed props')
    ap.add_argument('--no-probes', dest='probes', action='store_false',
                    help='no misc_cubemap reflection probes over water')
    ap.add_argument('--margin', type=float, default=60.0,
                    help='keep render triangles within this many metres of the tile collision (negative: keep all)')
    ap.add_argument('--clip-types', default='', help='comma list of COL types to make playerclip (3 always is)')
    ap.add_argument('--skip-types', default='', help='comma list of COL types to drop')
    ap.add_argument('--nier-tools', default=os.environ.get('NIER_TOOLS'),
                    help='the 2b-smallsword checkout (default $NIER_TOOLS)')
    ap.add_argument('--game', default=os.environ.get('NIER_DATA'),
                    help="the game's data folder, holding data003.cpk (default $NIER_DATA)")
    a = ap.parse_args(argv)
    if not a.nier_tools or not a.game:
        ap.error('give --nier-tools and --game (or set NIER_TOOLS and NIER_DATA)')
    global GAME
    GAME = Path(a.game)
    cpk, eff, texture, wmb = load_tools(a.nier_tools)
    ex = Exporter(a, Archives(cpk, eff), texture, wmb)
    w = ex.w
    g_render, g_props, g_col, g_ent = w.node('render'), w.node('props'), w.node('collision'), w.node('entities')
    ground = []
    for tile in a.tiles:
        ex.tile(tile, g_render, g_col, g_props, ground)
    blockers = np.concatenate(ex.blockers) if ex.blockers else None
    spawns = spawn_points(np.concatenate(ground) if ground else np.zeros((0, 3, 3)), a.spawns,
                          blockers=blockers, water=ex.water_levels)
    for i, p in enumerate(spawns):
        w.node('spawn%d' % i, translation=p, extras={'lp': lp.ROLE_ENTITY, 'classname': 'info_player_deathmatch',
                                                    'angle': str(90 * (i % 4))}, parent=g_ent)
    # reflection probes over the water; rd-vulkan's own fallback (one per spawn) is lost once any is placed, so the
    # spawns get theirs back. Other renderers ignore misc_cubemap.
    probes = probe_points(ex.water_levels, blockers) if a.probes else []
    if len(probes):
        for i, p in enumerate(list(probes) + [s + np.array([0, 0.6, 0]) for s in spawns]):
            w.node('probe%d' % i, translation=p, extras={'lp': lp.ROLE_ENTITY, 'classname': 'misc_cubemap',
                                                        'radius': '1000'}, parent=g_ent)
        ex.stats['probes'] = len(probes) + len(spawns)
    w.save(a.out, extras={'levelport': {'version': 1, 'units_per_metre': lp.UNITS_PER_METRE, 'source': ' '.join(a.tiles),
                                        'source_game': 'nier_automata', 'worldspawn': {
                                            'message': 'NieR:Automata %s (%s)' % (' '.join(a.tiles), a.variant)}}})
    print('%s -> %s (%.1f MB)' % (' '.join(a.tiles), a.out, Path(a.out).stat().st_size / 1e6))
    print('  ' + ', '.join('%s %d' % kv for kv in ex.stats.items()))


if __name__ == '__main__':
    main()
