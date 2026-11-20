"""w3decals.py -- a Witcher 3 level's projected decals as the render scene's maps/<map>.xdcl and a decal image pack
(Beauclair castle). The file format is cp77decals.py's (version 2): rd-vulkan projects each record's box onto the depth
buffer into its DBuffer. The xdcl writer, its validator and the build numbering are cp77decals.py's, imported.

Two stages:

    blender -b --python tools/levelport/w3decals.py -- read --layers LIST --out DIR
        (headless Blender 5.1 with the Witcher3-Blender-Tools add-on; w3patches.apply() first, as w3grade.py does)
        DIR/decals_raw.json: every packed decal in the listed layers and each texture's .xbm -> .dds (the add-on's
        _convert_xbm_to_writable_dds, into the uncook tree beside the .xbm)
    python -I tools/levelport/w3decals.py build --raw DIR/decals_raw.json --work WORK --build-json WORK/castle-4.build.json
        [--map w3_castle] [--builds .codex-research/levelport/build] [--max-px 1024] [--verify DIR/verify]
        writes <builds>/<map>_decals-<n>/ (a new number; built in a .partial folder): zz_<map>_decals.pk3 (maps/<map>
        .xdcl and textures/w3decals/*.dds, stored), maps/<map>.xdcl (a loose copy), <map>.xdcl.json (summary,
        conventions, materials, verification counts); with --verify the offline checks' images in that folder

LIST: one layer a line, relative to city_of_beauclair/ or a full dlc/ depot path (w3gltf.layer_paths). A layer that
fails to read is printed and the read stage exits 2 after writing decals_raw.json; a build whose pack fails its
validation exits 2 and leaves its .partial folder, not renamed into place.

The source. Witcher 3 cooks a layer's static decals (CDecalComponent) into the layer's CSectorData as packed objects
(BlockDataObjectType.Decal, the add-on's SBlockDataDecal): no component chunks are left in the layers, which is why
the Blender import brought in nothing for decals.w2l. A packed decal has a 3x3 matrix (rows: the box's local X, Y, Z
axes in world space, each scaled by the box's size; the box is a unit cube [-0.5, 0.5]^3, as the sector's resource
boxes say), a position (game metres, Z up), flags, and SBlockDataDecal: diffuseTexture (an index into the sector's
resources: a .xbm), specularColor, normalTreshold (degrees), specularity, fadeTime. The castle's 31 layers hold 572:
solid/decals.w2l 268, solid/upper_palace/decals.w2l 123, solid/lower_palace/walls.w2l 175,
solid/upper_palace/decorations.w2l 6; 62 textures, all diffuse with alpha (TCM_DXTAlpha, WorldDiffuseWithAlpha /
CharacterDiffuseWithAlpha). There are no normal or specular maps on a packed decal.

The mapping.
  box        map = diag(2, 2, 2) L^-1 (scene / 37.5 + origin - position): L's columns are the rows of the packed
             matrix, origin the build json's (scene units = (game - origin) x 37.5, no axis change: w3scene.py's).
             No axis is flipped: W3's local +Z is already out of the decorated surface and local +Y already runs down
             the image (V = 0 at the DDS's first row), so W3's box is the contract's box as it is (CP2077's needed Y
             negated). Measured, below.
  flags      W3's packed flags bit 0 is a vertical flip (contract flag 2), bit 1 a horizontal flip (contract flag 1).
             Normal threshold on (flag 4) for every record, W3's degrees written as they are; receivers static, road
             and terrain (256 | 512 | 1024: W3's static decals do not land on characters).
  threshold  W3's 0-57 degrees reads as rd-vulkan's default test (r_scenePDecalNormal 1: a receiver turned more than
             90 - threshold from +Z is left out), CP2077's reading: 35 castle decals have 0 and sit on rough cliff
             faces 20-50 degrees off their axis (a "more than threshold" reading would hide them all).
  hide       the sector object's radius (66-305 m: the streaming radius the game keeps the decal within) x 37.5.
  material   one per texture: the albedo image (RGB the colour as stored, sRGB; A the coverage: the texture's alpha),
             albedo alpha 1, tint 1, no normal or rough image, flags 0. orderNo 0 for all (W3 has none): records sort
             by material, so overlaps draw in texture order, not the sector's.
  images     textures/w3decals/<sha1 of the depot path>_<name>.dds: the texture's RGBA (Pillow decodes the add-on's
             DXT1/DXT5 DDS), halved down to --max-px on its longer side, BC7 with mips (cp2gltf.dds_bytes).
  dropped    decals outside the build box, a degenerate box (a side under 1 mm), a missing texture.
  not carried  specularity (-1 on 545, 1 on 27) and specularColor (0xff303030 on 568): W3's specular workflow; with
             no rough slot written the receiver keeps its own. fadeTime (0.25 s on all: a fade-in). Entity templates'
             own decal components (a layer entity's .w2ent): none of the castle's layers' entities was checked.

Verified offline (the build's --verify): the records decode back (validate: counts, paths in the pack, indices,
finite values, sort, invertible maps), each box centre maps back to its game position, and the boxes are cast
against the castle's LOD0 meshes (work-3 placements + meshes) and terrain (9 x 9 rays down the box's depth): the share
of rays that meet a surface inside the box, and whether the first surface met from the +Z face is a front face
(+Z out) -- 94% front faces from +Z, 20% from -Z, so +Z is out of the surface; leak textures (alpha heavy
in the first rows) on walls with flags 0 have local +Y pointing down in the world (leaking_long 22 of 22), with bit 0
set up (bob_leaking_uber_long 5 of 5), so +Y is down the image and bit 0 the vertical flip. The images: top.png (the
boxes' footprints over a height-shaded top view, coloured by their hit share), panels.png (decals as projected:
the receiving surface inside each box, shaded, with the decal's colour where it lands, in its UV frame with the world's
up marked), contact.png (every image, alpha over a checkerboard). Castle build w3_castle_decals-1: 572 decals, 62
materials, 62 images (57 MB BC7 at 1024 px), validation ok, centres back within 2e-5 m; 466 boxes meet a surface
on >= 50% of their rays (285 on >= 90%), 64 on some, 42 on none (25 with no mesh or terrain triangle near: geometry
the port leaves out or has not got, e.g. entity meshes; 27 of the 42 in solid/decals.w2l). The fresco
(bob_fresk_big_01_b, flags 0) and the leaks read upright in panels.png.

Open: U's sign is not measurable on the castle (no text or arrow decal): local +X is taken as +U, so on a
right-handed box with +Z out and +Y down the image an image reads mirrored from outside -- if W3 samples u = 0.5 - x
every record needs flag 1 toggled (one line: FLIP_U_ALL). The renderer's threshold test mode is a global cvar, not
per file; W3 and CP2077 agree on mode 1, so nothing to change.
"""
import argparse
import collections
import hashlib
import importlib
import json
import os
import sys
import time
import zipfile
from pathlib import Path

PKG = 'bl_ext.user_default.witcher3_tools'
BS = chr(92)
UPM = 37.5
IMG_DIR = 'textures/w3decals/'
FLIP_U_ALL = False          # the open question above: True toggles every record's horizontal flip
W3_FLIP_V, W3_FLIP_U = 1, 2  # W3 packed decal flags


# ---------------------------------------------------------------------------------------------------- read (Blender)

def read_stage(a):
    """(Blender) the layers' packed decals and their textures as DDS: <out>/decals_raw.json"""
    mat = importlib.import_module(PKG + '.repo_paths.materialize')
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))   # (w3patches.py, w3gltf.py)
    import w3patches
    import w3gltf
    w3patches.apply()
    cf = importlib.import_module(PKG + '.CR2W.CR2W_file')
    H = importlib.import_module(PKG + '.CR2W.CR2W_helpers')
    pu = importlib.import_module(PKG + '.CR2W.prop_utils')
    matmod = importlib.import_module(PKG + '.materials.material')
    DECAL = H.Enums.BlockDataObjectType.Decal
    out = Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    decals, per_layer, textures = [], {}, {}
    failed = []
    for path in w3gltf.layer_paths(a.layers):
        rel = path[len(w3gltf.CITY):] if path.lower().startswith(w3gltf.CITY) else path
        dp = path.replace('/', BS)
        try:
            f = cf.read_CR2W(mat.materialize_repo_path(dp, source_game='w3'))
        except Exception as e:
            per_layer[rel] = {'error': repr(e)[:300]}
            failed.append(rel)
            print('w3decals: %s: cannot read: %s' % (rel, repr(e)[:200]), flush=True)
            continue
        n, other = 0, collections.Counter()
        for c in f.CHUNKS.CHUNKS:
            if 'ecal' in c.name:
                other[c.name] += 1      # a decal component left unpacked (none in the castle's layers)
            if c.name != 'CSectorData':
                continue
            res = getattr(c, 'Resources', [])
            objs = getattr(c, 'Objects', [])
            for bi, b in enumerate(getattr(c, 'BlockData', [])):
                if b.packedObjectType != DECAL:
                    continue
                m, o = b.rotationMatrix, b.packedObject
                ob = objs[bi] if bi < len(objs) else None
                tex = res[o.diffuseTexture].pathHash if o.diffuseTexture < len(res) else 0
                decals.append({
                    'layer': rel, 'block': bi,
                    'pos': [b.position.x, b.position.y, b.position.z],
                    'rows': [[m.ax, m.ay, m.az], [m.bx, m.by, m.bz], [m.cx, m.cy, m.cz]],
                    'flags': b.flags, 'streaming_radius': b.streamingRadius,
                    'object_radius': getattr(ob, 'radius', None), 'object_flags': getattr(ob, 'flags', None),
                    'texture': str(tex) if tex else '', 'specular_color': o.specularColor,
                    'normal_threshold': o.normalTreshold, 'specularity': o.specularity, 'fade_time': o.fadeTime})
                n += 1
        per_layer[rel] = {'decals': n, 'unpacked_decal_chunks': dict(other)}
        print('w3decals: %s: %d decals' % (rel, n), flush=True)
    for t in sorted({d['texture'] for d in decals if d['texture']}):
        rec = {}
        try:
            xbm = mat.materialize_repo_path(t, source_game='w3')
            rec['xbm'] = xbm
            try:
                x = cf.read_CR2W(xbm)
                c0 = x.CHUNKS.CHUNKS[0]
                rec['props'] = {p.theName: str(pu.prop_to_string(p))[:120] for p in (c0.PROPS or [])
                                if p.theName in ('textureGroup', 'compression', 'width', 'height', 'format')}
            except Exception as e:
                rec['props_error'] = repr(e)[:200]
            rec['dds'] = matmod._convert_xbm_to_writable_dds(xbm) or ''
        except Exception as e:
            rec['error'] = repr(e)[:300]
        textures[t] = rec
        print('w3decals: texture %s -> %s' % (t, rec.get('dds') or rec.get('error')), flush=True)
    with open(out / 'decals_raw.json', 'w', encoding='utf-8') as fp:
        json.dump({'layers': per_layer, 'decals': decals, 'textures': textures}, fp, indent=1)
    print('w3decals: %d decals, %d textures -> %s' % (len(decals), len(textures), out / 'decals_raw.json'))
    if failed:
        print('w3decals: %d layers could not be read: %s' % (len(failed), ', '.join(failed)), file=sys.stderr)
        sys.exit(2)


# ---------------------------------------------------------------------------------------------------- the mapping

def frame(d):
    """(position, L): L's columns are the box's local X, Y, Z in game space, scaled by its size"""
    import numpy as np
    return np.array(d['pos'], float), np.array(d['rows'], float).T


def box_map(d, origin):
    """the 3x4 map from scene space (game units) to the box's [-1, 1]^3 (contract: x -> U, y -> V, z out)"""
    import numpy as np
    p, L = frame(d)
    A = np.diag([2.0, 2.0, 2.0]) @ np.linalg.inv(L)
    M = np.zeros((3, 4))
    M[:, :3] = A / UPM
    M[:, 3] = A @ (np.asarray(origin, float) - p)
    return M


def record_flags(d, cp):
    f = cp.DF_THRESHOLD | cp.ALL_STATIC
    if d['flags'] & W3_FLIP_V:
        f |= cp.DF_FLIP_V
    if bool(d['flags'] & W3_FLIP_U) != FLIP_U_ALL:
        f |= cp.DF_FLIP_U
    return f


def load_rgba(path, max_px):
    """the texture's RGBA (uint8, first row on top as stored), halved until its longer side fits max_px"""
    import numpy as np
    from PIL import Image
    im = Image.open(path)
    im.load()
    im = im.convert('RGBA')
    while max(im.size) > max_px:
        im = im.resize((max(1, im.size[0] // 2), max(1, im.size[1] // 2)), Image.BOX)
    # (BOX on RGBA as Pillow does it is not premultiplied: the colour under alpha 0 keeps its own value)
    return np.asarray(im, np.uint8).copy()


# ---------------------------------------------------------------------------------------------------- geometry

def load_geometry(work, decals, near_m=120.0):
    """per decal, the world triangles (Nx3x3, game metres, front faces counter-clockwise) of the castle's LOD0 meshes
    and terrain whose bounds meet its box"""
    import numpy as np
    here = Path(__file__).resolve().parent
    sys.path.insert(0, str(here))
    import gltfio
    work = Path(work)
    P = np.array([d['pos'] for d in decals])
    L = np.array([frame(d)[1] for d in decals])
    corners = np.array([[x, y, z] for x in (-.5, .5) for y in (-.5, .5) for z in (-.5, .5)])
    W = P[:, None, :] + np.einsum('nij,kj->nki', L, corners)
    lo, hi = W.min(1) - 0.05, W.max(1) + 0.05
    pl = json.load(open(work / 'placements.json', encoding='utf-8'))['placements']
    index = json.load(open(work / 'meshes' / 'index.json', encoding='utf-8'))
    docs, tris = {}, collections.defaultdict(list)
    all_tris = []
    for p in pl:
        if p['kind'] != 'w2mesh' or p['mesh'] not in index:
            continue
        M = np.array(p['matrix'], float)
        t = M[:3, 3]
        if p['mesh'] not in docs:
            d = gltfio.Document(work / 'meshes' / index[p['mesh']]['file'])
            prims = d.doc['meshes'][0]['primitives']
            parts = [pos[tri] for pr, (pos, _n, _uv, tri, _m) in zip(prims, d.primitives(0))
                     if pr.get('extras', {}).get('lod', 0) == 0]
            docs[p['mesh']] = np.concatenate(parts) if parts else np.zeros((0, 3, 3))
        V = docs[p['mesh']]
        if not len(V):
            continue
        Vw = V @ M[:3, :3].T + t
        if np.linalg.det(M[:3, :3]) < 0:
            Vw = Vw[:, [0, 2, 1]]
        all_tris.append(Vw.astype(np.float32))
        near = np.where(np.linalg.norm(P - t, axis=1) < near_m)[0]
        if not len(near):
            continue
        tlo, thi = Vw.min(1), Vw.max(1)
        blo, bhi = tlo.min(0), thi.max(0)
        for i in near:
            if np.any(bhi < lo[i]) or np.any(blo > hi[i]):
                continue
            k = np.all(thi >= lo[i], 1) & np.all(tlo <= hi[i], 1)
            if k.any():
                tris[i].append(Vw[k])
    tz = np.load(work / 'terrain' / 'tiles.npz')
    for name in tz['tiles']:
        g = tz['grid_' + str(name)]
        a, b, c, e = g[:-1, :-1], g[:-1, 1:], g[1:, :-1], g[1:, 1:]
        T = np.concatenate([np.stack([a, b, e], 2).reshape(-1, 3, 3), np.stack([a, e, c], 2).reshape(-1, 3, 3)])
        all_tris.append(T.astype(np.float32))
        tlo, thi = T.min(1), T.max(1)
        for i in range(len(decals)):
            k = np.all(thi >= lo[i], 1) & np.all(tlo <= hi[i], 1)
            if k.any():
                tris[i].append(T[k])
    return {i: np.concatenate(v) for i, v in tris.items()}, all_tris


def cast(O, D, T):
    """first hit per ray (t in [0, 1] along D, inf for none) and the hit triangle's unit normal"""
    import numpy as np
    best = np.full(len(O), np.inf)
    bn = np.zeros((len(O), 3))
    step = max(1, 2000000 // max(1, len(O)))
    for s in range(0, len(T), step):
        v0, v1, v2 = T[s:s + step, 0], T[s:s + step, 1], T[s:s + step, 2]
        e1, e2 = v1 - v0, v2 - v0
        pv = np.cross(D[:, None, :], e2[None])
        det = np.einsum('rtk,tk->rt', pv, e1)
        ok = np.abs(det) > 1e-12
        inv = np.where(ok, 1.0 / np.where(ok, det, 1.0), 0.0)
        tv = O[:, None, :] - v0[None]
        u = np.einsum('rtk,rtk->rt', tv, pv) * inv
        qv = np.cross(tv, e1[None])
        v = np.einsum('rk,rtk->rt', D, qv) * inv
        t = np.einsum('tk,rtk->rt', e2, qv) * inv
        hit = ok & (u >= 0) & (v >= 0) & (u + v <= 1) & (t >= 0) & (t <= 1)
        t = np.where(hit, t, np.inf)
        j = t.argmin(1)
        tj = t[np.arange(len(O)), j]
        better = tj < best
        n = np.cross(e1[j], e2[j])
        n /= np.linalg.norm(n, axis=1, keepdims=True) + 1e-30
        best = np.where(better, tj, best)
        bn[better] = n[better]
    return best, bn


def probe(d, T, n=9, inset=0.45):
    """rays down the box's depth from its +Z face and up from its -Z face on an n x n grid of (u, v)"""
    import numpy as np
    p, L = frame(d)
    A, B, C = L.T
    g = np.linspace(-inset, inset, n)
    uv = np.array([[u, v] for v in g for u in g])
    base = p + uv[:, :1] * A + uv[:, 1:] * B
    if not len(T):
        miss = np.full(len(base), np.inf)
        return miss, np.zeros((len(base), 3)), miss, np.zeros((len(base), 3))
    tp, npl = cast(base + 0.5 * C, np.tile(-C, (len(base), 1)), T)
    tm, nmi = cast(base - 0.5 * C, np.tile(C, (len(base), 1)), T)
    return tp, npl, tm, nmi


# ---------------------------------------------------------------------------------------------------- verify images

def top_view(all_tris, decals, stats, box, path, cell=0.5, zoom=None, mapname='w3_castle'):
    """the castle from above: height-shaded LOD0 meshes and terrain, the decal boxes' footprints by hit share"""
    import numpy as np
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    from matplotlib.patches import Polygon
    x0, y0, x1, y1 = zoom or box
    nx, ny = int((x1 - x0) / cell) + 1, int((y1 - y0) / cell) + 1
    H = np.full((ny, nx), -np.inf, np.float32)
    rng = np.random.default_rng(1)
    for T in all_tris:
        T = T.astype(np.float64)
        area = 0.5 * np.linalg.norm(np.cross(T[:, 1] - T[:, 0], T[:, 2] - T[:, 0]), axis=1)
        k = np.clip(np.ceil(area / (cell * cell * 0.25)), 1, 4000).astype(int)
        idx = np.repeat(np.arange(len(T)), k)
        r1, r2 = rng.random(len(idx)), rng.random(len(idx))
        s = np.sqrt(r1)
        pts = (1 - s)[:, None] * T[idx, 0] + (s * (1 - r2))[:, None] * T[idx, 1] + (s * r2)[:, None] * T[idx, 2]
        pts = np.concatenate([pts, T.reshape(-1, 3)])
        ix = ((pts[:, 0] - x0) / cell).astype(int)
        iy = ((pts[:, 1] - y0) / cell).astype(int)
        ok = (ix >= 0) & (ix < nx) & (iy >= 0) & (iy < ny)
        np.maximum.at(H, (iy[ok], ix[ok]), pts[ok, 2].astype(np.float32))
    empty = ~np.isfinite(H)
    Hf = np.where(empty, np.nanmin(np.where(empty, np.nan, H)), H)
    gy, gx = np.gradient(Hf, cell)
    shade = np.clip(0.55 + 0.45 * (-gx * 0.6 + gy * 0.6 + 1) / np.sqrt(gx * gx + gy * gy + 1) - 0.45, 0, 1)
    zn = (Hf - np.percentile(Hf, 2)) / max(1e-3, np.percentile(Hf, 98) - np.percentile(Hf, 2))
    img = np.clip(0.25 + 0.5 * np.clip(zn, 0, 1), 0, 1) * 0.5 + shade * 0.5
    img[empty] = 0.08
    w = 16 if zoom is None else 12
    fig, ax = plt.subplots(figsize=(w, w * ny / nx), dpi=150)
    ax.imshow(img, cmap='gray', origin='lower', extent=(x0, x1, y0, y1), vmin=0, vmax=1, interpolation='nearest')
    corners = np.array([[x, y, z] for x in (-.5, .5) for y in (-.5, .5) for z in (-.5, .5)])
    for d, s in zip(decals, stats):
        p, L = frame(d)
        c = (p + corners @ L.T)[:, :2]
        hull = convex_hull(c)
        hf = s['hit_share']
        col = (0.1, 0.85, 0.2) if hf >= 0.5 else (1.0, 0.6, 0.0) if hf > 0 else (1.0, 0.1, 0.1)
        ax.add_patch(Polygon(hull, closed=True, fill=False, ec=col, lw=0.6 if zoom is None else 1.0))
        C = L[:, 2] / np.linalg.norm(L[:, 2])
        if abs(C[2]) < 0.7:     # a wall decal: its +Z (out of the wall) as a tick
            ax.plot([p[0], p[0] + C[0] * 1.5], [p[1], p[1] + C[1] * 1.5], color=col, lw=0.6)
    ax.set_xlim(x0, x1)
    ax.set_ylim(y0, y1)
    ax.set_title(mapname + ' decals over LOD0 meshes + terrain (top view, game metres): green >= 50% of rays meet a '
                 'surface inside the box, orange some, red none; ticks: +Z of wall decals', fontsize=9)
    fig.tight_layout()
    fig.savefig(path)
    plt.close(fig)


def convex_hull(pts):
    import numpy as np
    pts = sorted(map(tuple, np.round(pts, 6)))
    if len(pts) <= 2:
        return np.array(pts)

    def half(ps):
        h = []
        for q in ps:
            while len(h) >= 2 and (h[-1][0] - h[-2][0]) * (q[1] - h[-2][1]) - (h[-1][1] - h[-2][1]) * (q[0] - h[-2][0]) <= 0:
                h.pop()
            h.append(q)
        return h
    lo, up = half(pts), half(pts[::-1])
    return np.array(lo[:-1] + up[:-1])


def panels(picks, decals, geo, rgba_of, path, n=56):
    """each picked decal as projected: in its UV frame (u right, v down, the record's flips applied), the first
    surface met from the +Z face inside the box shaded by its facing, the decal's colour over it by its coverage
    where the receiver passes the threshold test; a white arrow: the world's up in that frame"""
    import numpy as np
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    cols = 6
    rows = (len(picks) + cols - 1) // cols
    fig, axs = plt.subplots(rows, cols, figsize=(cols * 2.6, rows * 2.9), dpi=110)
    axs = np.atleast_1d(axs).ravel()
    for ax in axs:
        ax.axis('off')
    for ax, i in zip(axs, picks):
        d = decals[i]
        p, L = frame(d)
        T = geo.get(i, np.zeros((0, 3, 3)))
        tp, npl, _, _ = probe(d, T, n=n, inset=0.5 - 0.5 / n)
        C = L[:, 2] / np.linalg.norm(L[:, 2])
        hit = np.isfinite(tp)
        facing = npl @ C
        ang = np.degrees(np.arccos(np.clip(facing, -1, 1)))
        thr = d['normal_threshold']
        passes = hit & (ang <= 90 - thr) & (facing > 0)
        bg = np.where(hit, 0.25 + 0.6 * np.clip(facing, 0, 1), 0.0)
        img = np.stack([bg * 0.9, bg * 0.9, np.where(hit, bg, 0.25)], -1).reshape(n, n, 3)
        tex = rgba_of(d['texture'])
        if tex is not None:
            th, tw = tex.shape[:2]
            g = (np.arange(n) + 0.5) / n
            uu, vv = np.meshgrid(g, g)
            if d['flags'] & W3_FLIP_U:
                uu = 1 - uu
            if d['flags'] & W3_FLIP_V:
                vv = 1 - vv
            s = tex[np.minimum((vv * th).astype(int), th - 1), np.minimum((uu * tw).astype(int), tw - 1)] / 255.0
            a = (s[..., 3] * passes.reshape(n, n))[..., None]
            img = img * (1 - a) + s[..., :3] * a
        ax.imshow(img, interpolation='nearest', extent=(0, 1, 1, 0))
        up = np.linalg.solve(L, np.array([0, 0, 1.0]))[:2]     # the world's +Z in (u, v), flips applied
        if d['flags'] & W3_FLIP_U:
            up[0] = -up[0]
        if d['flags'] & W3_FLIP_V:
            up[1] = -up[1]
        if np.linalg.norm(up) > 1e-6:
            up = up / np.linalg.norm(up) * 0.3
            ax.annotate('', xy=(0.5 + up[0], 0.5 + up[1]), xytext=(0.5, 0.5),
                        arrowprops=dict(color='white', width=1.2, headwidth=5))
        ax.set_title('%d %s\nfl %d thr %.0f hit %.0f%%' % (i, d['texture'].split(BS)[-1][:24], d['flags'], thr,
                                                          100 * hit.mean()), fontsize=6)
    fig.suptitle('decals as projected (UV frame: u right, v down; grey: the receiving surface, blue: none in the box; '
                 'arrow: world up)', fontsize=8)
    fig.tight_layout()
    fig.savefig(path)
    plt.close(fig)


def contact_sheet(items, path, cell=200):
    """items: [(label, rgba uint8)]: alpha over a checkerboard, labelled"""
    import numpy as np
    from PIL import Image, ImageDraw
    cols = 8
    rows = (len(items) + cols - 1) // cols
    sheet = Image.new('RGB', (cols * cell, rows * (cell + 28)), (30, 30, 30))
    dr = ImageDraw.Draw(sheet)
    for k, (label, a) in enumerate(items):
        im = Image.fromarray(a)
        im.thumbnail((cell - 8, cell - 8), Image.BOX)
        w, h = im.size
        yy, xx = np.mgrid[0:h, 0:w]
        chk = np.where(((yy // 8 + xx // 8) % 2)[..., None] == 0, 200, 120).astype(np.uint8).repeat(3, 2)
        arr = np.asarray(im).astype(np.float32) / 255
        out = chk / 255 * (1 - arr[..., 3:]) + arr[..., :3] * arr[..., 3:]
        x, y = (k % cols) * cell + (cell - w) // 2, (k // cols) * (cell + 28) + (cell - h) // 2
        sheet.paste(Image.fromarray((out * 255).astype(np.uint8)), (x, y))
        lines = [label[i:i + 30] for i in range(0, len(label), 30)][:2]
        for j, ln in enumerate(lines):
            dr.text(((k % cols) * cell + 4, (k // cols) * (cell + 28) + cell - 2 + j * 12), ln, fill=(230, 230, 230))
    sheet.save(path)


# ---------------------------------------------------------------------------------------------------- build

def build_stage(a):
    import numpy as np
    here = Path(__file__).resolve().parent
    sys.path.insert(0, str(here))
    import cp77decals as cp
    import cp2gltf
    t0 = time.time()
    raw = json.load(open(a.raw, encoding='utf-8'))
    bj = json.load(open(a.build_json, encoding='utf-8'))
    origin = np.array(bj['origin'], float)
    x0, y0, x1, y1 = bj['box']
    builds = Path(a.builds)
    final = cp.next_build(builds, a.map + '_decals')
    stage = final.with_name(final.name + '.partial')
    stage.mkdir(parents=True)
    pk3 = stage / ('zz_%s_decals.pk3' % a.map)
    z = zipfile.ZipFile(pk3, 'w', zipfile.ZIP_STORED)

    images, mats, mat_of, meta, rgba_cache = [], [], {}, [], {}
    tex_bytes = 0

    def rgba_of(t):
        if t not in rgba_cache:
            rec = raw['textures'].get(t) or {}
            rgba_cache[t] = load_rgba(rec['dds'], a.max_px) if rec.get('dds') and os.path.exists(rec['dds']) else None
        return rgba_cache[t]

    def material(t):
        nonlocal tex_bytes
        if t in mat_of:
            return mat_of[t]
        arr = rgba_of(t)
        if arr is None:
            mat_of[t] = 'texture missing'
            return mat_of[t]
        stem = t.split(BS)[-1].rsplit('.', 1)[0].lower()
        name = (hashlib.sha1(t.lower().encode()).hexdigest()[:10] + '_' + stem)[:63 - len(IMG_DIR) - len('.dds')]
        path = IMG_DIR + name + '.dds'
        dds = cp2gltf.dds_bytes(np.ascontiguousarray(arr), True)
        z.writestr(zipfile.ZipInfo(path), dds, compress_type=zipfile.ZIP_STORED)
        tex_bytes += len(dds)
        images.append(path)
        mats.append({'albedo': len(images) - 1, 'normal': cp.NONE, 'rough': cp.NONE, 'flags': 0, 'albedo_alpha': 1.0,
                     'normal_alpha': 0.0, 'rough_alpha': 0.0, 'rough_bias': 0.0, 'uv': [0.0, 0.0, 1.0, 1.0],
                     'tint': [1.0, 1.0, 1.0], 'emissive': 0.0, 'height': 0.0, 'emissive_img': cp.NONE,
                     'tiled_colour': cp.NONE, 'tiled_normal': cp.NONE, 'tiled_rough': cp.NONE, 'period': 0.0,
                     'tiled_scale': [1.0, 1.0, 1.0], 'levels': [0.0, 1.0]})
        src = raw['textures'][t]
        meta.append({'texture': t, 'image': path, 'size': [int(arr.shape[1]), int(arr.shape[0])],
                     'source': src.get('props', {}), 'coverage_mean': round(float(arr[..., 3].mean() / 255), 4)})
        mat_of[t] = len(mats) - 1
        return mat_of[t]

    dropped, notes = collections.Counter(), collections.Counter()
    recs, kept = [], []
    for d in raw['decals']:
        p, L = frame(d)
        if not (x0 <= p[0] <= x1 and y0 <= p[1] <= y1):
            dropped['outside the build box'] += 1
            continue
        if min(np.linalg.norm(L, axis=0)) < 1e-3 or abs(np.linalg.det(L)) < 1e-9:
            dropped['degenerate box'] += 1
            continue
        if not d['texture']:
            dropped['no texture'] += 1
            continue
        m = material(d['texture'])
        if isinstance(m, str):
            dropped[m] += 1
            continue
        if np.linalg.det(L) < 0:
            notes['left-handed box (mirrored)'] += 1
        if d['specularity'] >= 0:
            notes['specularity set (not carried)'] += 1
        M = box_map(d, origin)
        r = {'map': [float(v) for v in M.ravel()], 'mat': m, 'alpha': 1.0,
             'hide': float((d.get('object_radius') or 0) * UPM), 'flags': record_flags(d, cp),
             'thr': float(min(max(d['normal_threshold'], 0.0), 90.0)), 'order': 0, 'rough_scale': 1.0,
             'cs_alpha': 1.0}
        recs.append(r)
        kept.append(d)
    z_order = sorted(range(len(recs)), key=lambda i: (recs[i]['order'], recs[i]['mat']))
    recs = [recs[i] for i in z_order]
    kept = [kept[i] for i in z_order]
    blob = cp.xdcl_blob(images, mats, recs)
    (stage / 'maps').mkdir(exist_ok=True)
    (stage / 'maps' / ('%s.xdcl' % a.map)).write_bytes(blob)
    z.writestr(zipfile.ZipInfo('maps/%s.xdcl' % a.map), blob, compress_type=zipfile.ZIP_STORED)
    z.close()

    # read the pack back: the contract's checks (cp77decals.validate, its image folder pointed at ours)
    with zipfile.ZipFile(pk3) as zz:
        names = set(zz.namelist())
        back = zz.read('maps/%s.xdcl' % a.map)
    old_dir = cp.IMG_DIR
    cp.IMG_DIR = IMG_DIR
    try:
        parsed, bad = cp.validate(back, names, {'images': len(images), 'materials': len(mats), 'decals': len(recs)})
    finally:
        cp.IMG_DIR = old_dir
    # each box's centre back to its game position, its +Z to the source's local Z
    worst_centre, worst_axis = 0.0, 0.0
    for r, d in zip(parsed['decals'], kept):
        M = np.array(r['map']).reshape(3, 4)
        c = np.linalg.solve(M[:, :3], -M[:, 3]) / UPM + origin
        worst_centre = max(worst_centre, float(np.linalg.norm(c - np.array(d['pos']))))
        zs = np.linalg.solve(M[:, :3], [0, 0, 1.0])
        C = np.array(d['rows'][2])
        worst_axis = max(worst_axis, float(1 - zs @ C / (np.linalg.norm(zs) * np.linalg.norm(C))))
    if worst_centre > 0.01 or worst_axis > 1e-4:
        bad.append('maps do not decode back: centre %.4f m, axis %.2g' % (worst_centre, worst_axis))

    summary = {
        'map': a.map, 'decals': len(recs), 'materials': len(mats), 'images': len(images),
        'source_decals': len(raw['decals']), 'dropped': dict(dropped), 'notes': dict(notes),
        'layers': {k: v for k, v in raw['layers'].items() if v.get('decals') or v.get('error')},
        'texture_bytes': tex_bytes, 'xdcl_bytes': len(blob), 'pk3_bytes': pk3.stat().st_size, 'xdcl_version': cp.VERSION,
        'origin': origin.tolist(), 'box': bj['box'], 'max_px': a.max_px,
        'decode_check': {'worst_centre_m': worst_centre, 'worst_axis_cos_err': worst_axis},
        'validation': bad,
        'conventions': {
            'box': 'unit cube [-0.5,0.5]^3 of the packed matrix (rows = local X, Y, Z scaled); no axis flipped: '
                   '+Z out of the surface, +Y down the image (V = 0 at the first row), +X = U',
            'flags': 'W3 bit 0 -> flip V (2), bit 1 -> flip U (1); threshold on (4); receivers 256|512|1024',
            'flip_u_all': FLIP_U_ALL,
            'threshold': "W3's degrees as written (rd-vulkan r_scenePDecalNormal 1: > 90 - thr from +Z rejected)",
            'hide': 'the sector object radius (m) x 37.5',
            'not_carried': ['specularity', 'specularColor', 'fadeTime'],
        },
        'flags_source': dict(collections.Counter(str(d['flags']) for d in kept)),
    }

    if a.verify:
        vt = time.time()
        geo, all_tris = load_geometry(a.work, kept)
        stats = []
        for i, d in enumerate(kept):
            tp, npl, tm, nmi = probe(d, geo.get(i, np.zeros((0, 3, 3))))
            C = np.array(d['rows'][2])
            C = C / np.linalg.norm(C)
            hp, hm = np.isfinite(tp), np.isfinite(tm)
            stats.append({'hit_share': float(hp.mean()), 'front_plus': float((npl[hp] @ C > 0).mean()) if hp.any() else None,
                          'front_minus': float((nmi[hm] @ C < 0).mean()) if hm.any() else None,
                          'triangles': int(len(geo.get(i, [])))})
        hs = np.array([s['hit_share'] for s in stats])
        fp = [s['front_plus'] for s in stats if s['front_plus'] is not None]
        fm = [s['front_minus'] for s in stats if s['front_minus'] is not None]
        summary['verify'] = {
            'hit_share_ge_50pct': int((hs >= 0.5).sum()), 'hit_share_ge_90pct': int((hs >= 0.9).sum()),
            'hit_share_some': int(((hs > 0) & (hs < 0.5)).sum()), 'hit_none': int((hs == 0).sum()),
            'no_triangles_near': int(sum(1 for s in stats if s['triangles'] == 0)),
            'front_face_share_from_plus_z': round(float(np.mean(fp)), 3) if fp else None,
            'front_face_share_from_minus_z': round(float(np.mean(fm)), 3) if fm else None,
            'misses': [{'layer': d['layer'], 'block': d['block'], 'pos': [round(v, 2) for v in d['pos']],
                        'texture': d['texture'].split(BS)[-1], 'triangles': s['triangles']}
                       for d, s in zip(kept, stats) if s['hit_share'] == 0],
            'seconds': round(time.time() - vt, 1)}
        top_view(all_tris, kept, stats, bj['box'], stage / 'top.png', mapname=a.map)
        P = np.array([d['pos'] for d in kept])
        # two close views: where the decals are densest
        cx, cy = np.median(P[:, 0]), np.median(P[:, 1])
        top_view(all_tris, kept, stats, bj['box'], stage / 'top_zoom_a.png', cell=0.2,
                 zoom=(P[:, 0].min() - 5, P[:, 1].min() - 5, cx + 5, cy + 5), mapname=a.map)
        top_view(all_tris, kept, stats, bj['box'], stage / 'top_zoom_b.png', cell=0.2,
                 zoom=(cx - 5, cy - 5, P[:, 0].max() + 5, P[:, 1].max() + 5), mapname=a.map)
        # panels: the first use of each texture, then the misses' and the flipped leaks, at most 48
        seen, picks = set(), []
        for i, d in enumerate(kept):
            if d['texture'] not in seen:
                seen.add(d['texture'])
                picks.append(i)
        picks = picks[:42] + [i for i, s in enumerate(stats) if s['hit_share'] == 0][:3] + \
            [i for i, d in enumerate(kept) if d['flags'] and 'leaking' in d['texture']][:3]
        panels(picks, kept, geo, rgba_of, stage / 'panels.png')
        uses = collections.Counter(d['texture'] for d in kept)
        contact_sheet([('%s x%d %dx%d' % (m['texture'].split(BS)[-1].rsplit('.', 1)[0], uses[m['texture']], *m['size']),
                        rgba_of(m['texture'])) for m in meta], stage / 'contact.png')

    out = {'summary': summary, 'images': images,
           'materials': [dict(meta[i], record=mats[i]) for i in range(len(mats))],
           'decals': [{'layer': d['layer'], 'block': d['block'], 'pos': d['pos'], 'texture': d['texture'],
                       'w3_flags': d['flags'], 'record': r} for d, r in zip(kept, recs)]}
    (stage / ('%s.xdcl.json' % a.map)).write_text(json.dumps(out, indent=1), encoding='utf-8')
    if bad:
        print('  validation: %s; decode: centre %.2g m, axis %.2g' % ('; '.join(bad[:10]), worst_centre, worst_axis))
        print('w3decals: the pack failed its validation; left in %s (not renamed into place)' % stage, file=sys.stderr)
        sys.exit(2)
    stage.rename(final)
    print('w3decals: %s' % (final / pk3.name))
    print('  decals %d of %d (dropped %s), materials %d, images %d (%.1f MB DDS), xdcl %.1f KB, pk3 %.1f MB' % (
        len(recs), len(raw['decals']), dict(dropped) or 'none', len(mats), len(images), tex_bytes / 1e6,
        len(blob) / 1e3, summary['pk3_bytes'] / 1e6))
    if 'verify' in summary:
        v = summary['verify']
        print('  verify: hit >= 50%% %d, some %d, none %d (no triangles near %d); front faces from +Z %.3f, from -Z %.3f'
              % (v['hit_share_ge_50pct'], v['hit_share_some'], v['hit_none'], v['no_triangles_near'],
                 v['front_face_share_from_plus_z'], v['front_face_share_from_minus_z']))
    print('  validation: %s; decode: centre %.2g m, axis %.2g (%.0f s)' % (
        'ok' if not bad else '; '.join(bad[:10]), worst_centre, worst_axis, time.time() - t0))
    return final


def main():
    argv = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else sys.argv[1:]
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    sub = ap.add_subparsers(dest='stage', required=True)
    r = sub.add_parser('read', help='(Blender) the layers\' packed decals and their textures')
    r.add_argument('--layers', required=True, help='layer list (paths under city_of_beauclair/, one a line)')
    r.add_argument('--out', required=True)
    b = sub.add_parser('build', help='the .xdcl, the pack and the summary (plain Python)')
    b.add_argument('--raw', required=True, help="the read stage's decals_raw.json")
    b.add_argument('--work', required=True, help='the work folder (placements, meshes, terrain: for --verify)')
    b.add_argument('--build-json', required=True, help="w3build.py's record (box and origin)")
    b.add_argument('--map', default='w3_castle')
    b.add_argument('--builds', default=str(Path(__file__).resolve().parents[2] / '.codex-research' / 'levelport' / 'build'))
    b.add_argument('--max-px', type=int, default=1024)
    b.add_argument('--verify', action='store_true', help='the offline checks and their images, into the build folder')
    a = ap.parse_args(argv)
    if a.stage == 'read':
        read_stage(a)
    else:
        build_stage(a)


if __name__ == '__main__':
    main()
