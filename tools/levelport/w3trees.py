"""w3trees.py -- the SpeedTree trees of a Witcher 3 work folder as ordinary meshes (Beauclair).

w3gltf.py records the placed SpeedTree components (`kind: "srt"`, a .srt depot path and its matrix) but dumps no mesh
for them, so w3scene.py skipped them. This script turns each distinct .srt into a mesh glb written the way w3gltf
writes a .w2mesh, adds Witcher3_Main-shaped material records for its bark, fronds and leaf cards, and rewrites the
tree placements as `kind: "w2mesh"`, so the existing w3scene / w3_preview read them unchanged.

    # in Blender (materializes the .srt files, their JSON and the textures the uncook folder lacks), then writes:
    blender -b --python tools/levelport/w3trees.py -- --work DIR [--w3 RESEARCH]
    # plain Python once those files exist (writes only; lists anything missing and stops):
    python tools/levelport/w3trees.py --work DIR [--update [--backup pre-update]] [--images DIR]
    # in Blender, the billboards' top images from the trees' LOD 0 glbs (before the write that uses them; one process):
    blender -b --python tools/levelport/w3trees.py -- --work DIR --bake-tops [--trees stem,stem]

Reads (work folder, from w3gltf.py): placements.json, materials.json, meshes/index.json. On the first run each is
copied beside itself as *.pre-trees.json (placements.pre-trees.json, materials.pre-trees.json,
meshes/index.pre-trees.json); every later run starts from those copies, so re-running replaces the trees instead of
stacking them. Nothing is deleted. A run whose copies are older than another step's last run (w3foliage --merge
again, say) is refused, as it would drop that run's output (w3work.py); --rebase sets the copies aside and starts
from the current files, or --update works on them in place.
--update: in place on the current files instead, for a work folder that later steps (water, curtains,
cloth) have built on since the trees: only the trees' glbs, index entries, new records and their placements' slots
change (each placement keeps its names for the same records), each file first copied once to *.pre-update.json and
each tree glb to meshes/pre-update/ (--backup NAME: *.NAME.json and meshes/NAME/, for a second update that must keep
the first one's state).
Reads (game data): <srt>.json beside each .srt in the uncook folder (the io_mesh_srt converter's output of the raw
`SRT 07` blob; w3patches unwraps the CR2W container) and the .dds images it names, in the .srt's folder or, where the
uncook folder lacks them, under --images (default <work>/srt_images/<the .srt's depot folder>/, where the Blender step
now extracts them: the billboard atlases; the uncook folder is not written).

Writes:
    meshes/<key>.glb    one per .srt (key as w3gltf's mesh_key): a primitive per SpeedTree draw call, extras {lod,
                        distance, material (index into the placement's slots)}; POSITION, NORMAL, TEXCOORD_0. No
                        TANGENT (w3scene rebuilds the frame from the UVs) and no COLOR_0 (no layer blends here).
                        The billboard, where there is one, is the next LOD: the vertical card and the top card, a
                        primitive each (extras billboard {part: card | top | fixed}).
    meshes/index.json   + {srt depot path: {file, materials, lods, kind: 'srt'}}
    materials.json      + one record per distinct (diffuse, normal, cutout) set, named srt_<diffuse stem>[_<normal
                        stem>][_cut]; a tree's vertical card srtbb_<tree stem> (billboard 'card', card {type 1, views,
                        a0, uv}: w3scene writes it to maps/<map>.xw3b version 3), its top card srttop_<tree stem>
                        (billboard 'top', card {type 2}), a flat tree's fixed planes srtbbf_<atlas stem> ('fixed')
    <images>/_bbc/      the billboard normal atlases in card axes (CardNormals), <stem>_<hash>.png
    <images>/_bbn/      the billboard normal atlases in image axes (BillboardNormals; the flat trees' fixed planes)
    srt_tops/           the baked top images (--bake-tops): <tree key>_c.png (colour, coverage alpha), <key>_n.png
                        (normals), raw/ (the renders), tops.json (per tree its images and the card's square)
    placements.json     the srt placements as kind 'w2mesh' (mesh = the .srt depot path, slots = the mesh's
                        materials, srt_source: true); every other entry unchanged
    trees.json          counts and per-tree LODs (beside placements.json)

Conventions (checked against the add-on and its own import):
- Space: SpeedTree positions are already mesh-local metres, +Z up, the same axes as the .w2mesh glbs; the placement
  matrix (the add-on's object matrix) maps them to game space. No axis change.
- UV: the JSON's `diffuse` UVs as stored (the game's v-down); io_mesh_srt flips v for Blender as dc_mesh does, and
  w3gltf writes the unflipped value, so this matches the existing glbs.
- Normals: the vertex bytes as io_mesh_srt decodes them, (b / 255 - 0.5) * 2, normalised. Winding: bark and caps
  agree with their normals counter-clockwise (>= 0.99 of triangles, as the .w2mesh glbs); leaves are two-sided.
- Leaf cards (fixed cards as imported, no camera-facing material): a facing leaf vertex is
  its card centre plus the card corner reordered (z, x, y), what the add-on's Leaf_Card node gives with no camera
  (the headless import w3gltf read); every such card faces mesh-local +X.
- Normal maps: DirectX (green down). The add-on's SRT_Material inverts green before its Normal Map node, as the
  .w2mesh materials need; w3scene's dx_to_gl_normal makes them OpenGL, so they pass through unchanged.
- Alpha: a draw call whose render state has BDiffuseAlphaMaskIsOpaque false is a cutout: the record carries the
  diffuse alpha as `Alpha` and blend_method CLIP, which w3scene's record() reads as kind 'cutout' (two-sided; an
  opaque albedo still falls back to opaque there). The add-on's material tests alpha at 0.5 (its alpha-scalar node
  divides FAlphaScalar by itself), as the renderer's cutout does, so the images are used as they are.
- LODs: every SpeedTree LOD; LOD 0 from 0 m, LOD k >= 1 from m_fHighDetail3dDistance + (Low - High) * (k - 1) /
  (n - 2) (the last at m_fLowDetail3dDistance; a 2-LOD tree switches at High). Grass (BUsedAsGrass): the first draw
  call of the first LOD, as io_mesh_srt imports it.
- Billboards: the game swaps a tree for its SpeedTree billboard from m_fBillboardStartDistance (fading
  until m_fBillboardFinalDistance); here the billboard is a last LOD from the start distance, where it has at most half
  the last 3D LOD's triangles and that LOD has at least BB_MIN (the city's vineyards, 15 triangles, keep theirs).
  The SRT holds VerticalBillboards (NNumBillboards views around the trunk, FWidth/FTopPos/FBottomPos, a texcoord
  rectangle and PRotated per view, a cutout polygon) and the atlas pair in ABillboardRenderStateMain (*_Billboards.dds,
  *_Billboards_n.dds); no tree of the city has a HorizontalBillboard. Game-style (renderer scene.vert / scene.frag -9 / -10): a vertical card the
  renderer turns about the tree's up axis to face the viewer, mapping its (s, t) to the stored view nearest the viewing
  direction (view_maps: view i seen from mesh azimuth BB_A0 + 2 pi i / n, unmirrored; check_views in the research
  folder), and a horizontal top card (the canopy baked from straight above, --bake-tops) it fades to as the view looks
  down. The crossed planes before it (83d4ac2b8) thinned out from above and showed as a star.
  Flat trees (LOD 0's horizontal spread under BB_FLAT: wall vines and roses, hedges, lying branches; and every
  wall_plant, whatever its spread) keep fixed planes
  (billboard_planes: two along LOD 0's principal axes): a vine on a wall must not turn to face the viewer, and no top
  card (their fixed planes do not fade). Material srtbbf_*, an ordinary cutout: no .xw3b record.
  Materials: cutout (alpha 0.5, two-sided), not srt_ named, so without the canopy shading (flag 0x800): that bends the
  normal toward the mesh's sphere centre and darkens it near the centre, which on a billboard is the canopy's face toward
  the eye; the billboard's own normal map already holds the rounded canopy's normals. One vertical-card material per
  tree (each tree's views sit in its own frames of the species' shared atlas, and the record carries them).
- Not carried: wind, hue variation, the detail layer (castanea's stem detail), branch-seam blending, AO bytes,
  SpecularColor/shininess (w3scene's default F0 and roughness 0.6 apply).
"""
import argparse
import hashlib
import json
import os
import shutil
import sys
from collections import Counter, OrderedDict

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import gltfio  # noqa: E402
import w3paths  # noqa: E402
import w3work  # noqa: E402

BS = chr(92)
PKG = 'bl_ext.user_default.witcher3_tools'
GEOM = ('BBranchesPresent', 'BFrondsPresent', 'BLeavesPresent', 'BFacingLeavesPresent', 'BRigidMeshesPresent')


def mesh_key(path):
    """w3gltf.mesh_key: the file stem (40 chars) and a hash of the depot path."""
    stem = path.rsplit(BS, 1)[-1].rsplit('.', 1)[0][:40]
    return stem + '_' + hashlib.sha1(path.encode('utf-8')).hexdigest()[:10]


def uncook_file(uncook, depot_path):
    return os.path.join(uncook, *depot_path.replace('/', BS).split(BS))


def texture_names(srt):
    """The image names (main 3D render states and the billboards', slots 0 diffuse and 1 normal) an SRT JSON uses."""
    out = []
    g = srt['Geometry']
    states = list(g['P3dRenderStateMain'])
    if billboard_views(srt):
        states.append(g['ABillboardRenderStateMain'])
    for r in states:
        for t in r['ApTextures'][:2]:
            if t and t not in out:
                out.append(t)
    return out


def billboard_views(srt):
    """The SRT's vertical billboards ({width, top, bottom, views: [(u, v, du, dv, rotated)], cutout: (verts, tris)}), or
    None (grass, or a tree compiled without them)."""
    vb = srt.get('VerticalBillboards') or {}
    n = int(vb.get('NNumBillboards') or 0)
    rs = srt['Geometry'].get('ABillboardRenderStateMain') or {}
    if n <= 0 or not (rs.get('ApTextures') or [''])[0] or any(r.get('BUsedAsGrass')
                                                               for r in srt['Geometry']['P3dRenderStateMain']):
        return None
    tc = vb['PTexCoords'][:n]
    rot = list(vb.get('PRotated') or [False] * n)[:n]
    views = [(t['x'], t['y'], t['z'], t['w'], bool(r)) for t, r in zip(tc, rot)]
    nc = int(vb.get('NNumCutoutVertices') or 0)
    if nc >= 3 and vb.get('PCutoutIndices'):
        cv = np.array([[c['x'], c['y']] for c in vb['PCutoutVertices'][:nc]], np.float64)
        ct = np.array(vb['PCutoutIndices'], np.int64).reshape(-1, 3)
    else:
        cv = np.array([[0.0, 0.0], [1.0, 0.0], [1.0, 1.0], [0.0, 1.0]])
        ct = np.array([[0, 1, 2], [2, 3, 0]], np.int64)
    return {'width': float(vb['FWidth']), 'top': float(vb['FTopPos']), 'bottom': float(vb['FBottomPos']),
            'views': views, 'cutout': (cv, ct), 'state': rs}


# ---------------------------------------------------------------------------------------------------- Blender step

def find_image(folder, name, img_root, srt_path):
    """An SRT image: beside the .srt in the uncook folder, else as prepare() extracted it under img_root (the .srt's
    depot folder); the uncook-folder path when neither exists (reported missing)."""
    p = os.path.join(folder, name)
    if os.path.exists(p) or not img_root:
        return p
    q = os.path.join(img_root, *os.path.dirname(srt_path.replace('/', BS)).split(BS), name)
    return q if os.path.exists(q) else p


def prepare(srt_paths, img_root=None):
    """In Blender: materialize each .srt (unwrapped by w3patches), its JSON sidecar and the images it names (the
    add-on's own SRT texture lookup in texture.cache). Images go beside the .srt, or with img_root under
    img_root/<the .srt's depot folder> (the billboard atlases: the uncook folder stays as it was)."""
    import importlib
    import bpy  # noqa: F401
    sys.path.insert(0, HERE)   # (w3patches.py)
    import w3patches
    w3patches.apply()
    mat = importlib.import_module(PKG + '.repo_paths.materialize')
    fb = importlib.import_module(PKG + '.ui.ui_file_browser')
    tcm = importlib.import_module(PKG + '.CR2W.witcher_cache.TextureCache')
    manager = None
    report = {}
    for path in srt_paths:
        r = report[path] = {'extracted': [], 'missing': []}
        try:
            ab = mat.materialize_repo_path(path, source_game='w3')
        except Exception as e:  # noqa: BLE001 - reported, the rest go on
            r['error'] = 'materialize: ' + repr(e)[:200]
            continue
        js = ab + '.json'
        if not os.path.exists(js):
            fb._srt_json_from_file(ab)          # w3patches unwraps the CR2W container first
        if not os.path.exists(js):
            r['error'] = 'no json'
            continue
        srt = json.load(open(js, encoding='utf-8'))
        folder = os.path.dirname(ab)
        rel_folder = os.path.dirname(path.replace('/', BS))
        for t in texture_names(srt):
            out = find_image(folder, t, img_root, path)
            if os.path.exists(out):
                continue
            if img_root:
                out = os.path.join(img_root, *rel_folder.split(BS), t)
                os.makedirs(os.path.dirname(out), exist_ok=True)
            if manager is None:
                manager = tcm.LoadTextureManager(loadmods=False)
            item = fb._choose_srt_texture_cache_item(manager, t, rel_folder)
            if item is None:
                r['missing'].append(t)
                continue
            item.extract_to_file(out)
            (r['extracted'] if os.path.exists(out) else r['missing']).append(t)
        print('W3TREES prepare', path.rsplit(BS, 1)[-1], 'extracted', len(r['extracted']), 'missing', r['missing'],
              flush=True)
    return report


TOPS = 'srt_tops'


def lod0_parts(work, rec, materials, srt=None, facing=None):
    """A tree glb's first LOD: [(positions, normals, uv, triangles, material name, cutout)]. With srt (its JSON) and
    facing (the viewer's right, up and toward-the-viewer axes, 3 x 3 rows), the facing leaf cards turned to that viewer
    (the Leaf_Card node with a camera: centre + right c0 + up c1 + toward c2; the glbs keep them fixed toward +X, which
    is right (0, 1, 0), up (0, 0, 1), toward (1, 0, 0)), as the game draws them, and as SpeedTree rendered the billboard
    views."""
    doc = gltfio.Document(os.path.join(work, 'meshes', rec['file']))
    prims = doc.doc['meshes'][0]['primitives']
    first = min(pr.get('extras', {}).get('lod', 0) for pr in prims)
    corners = []
    if srt is not None and facing is not None:
        # the draw calls in convert()'s order (the empty ones skipped), each one's corners or None
        for dc in srt['Geometry']['PLods'][first]['PDrawCalls']:
            vd = dc['VertexData']
            if len(vd['pos']) == 0 or len(dc['IndexData']) == 0:
                continue
            c = vd.get('leaf_card_corner')
            corners.append(np.array(c, dtype=np.float64).reshape(-1, 3) if c else None)
    out = []
    k = 0
    for pr, (pos, nrm, uv, tri, _m) in zip(prims, doc.primitives(0)):
        ex = pr.get('extras', {})
        if ex.get('lod', 0) != first:
            continue
        name = rec['materials'][ex.get('material', 0)]
        c = corners[k] if k < len(corners) else None
        k += 1
        if c is not None and len(c) == len(pos):
            pos = pos - c[:, [2, 0, 1]] + c @ np.asarray(facing, np.float64)
        out.append((pos, nrm, uv, tri, name, (materials.get(name) or {}).get('blend_method') == 'CLIP'))
    return out


class BlenderTree:
    """In Blender: a tree's LOD 0 from its glb with unlit materials (the top bake, and the research folder's view check).
    Per materials.json record a 'colour' material (emission = the diffuse as stored, alpha the cutout's at 0.5) and a
    'normal' one (emission = the shading normal, the leaf normal maps applied (DirectX: green inverted first), written
    as a DirectX map in mesh axes: (x, -y, z) / 2 + 0.5; render it with the Raw view transform); 'normal_up' the same
    turned into the upper half (for a camera straight above)."""

    def __init__(self, materials):
        import bpy
        self.bpy, self.recs, self.mats, self.imgs = bpy, materials, {}, {}

    def image(self, path, colour):
        if not path or not os.path.exists(path):
            return None
        if path not in self.imgs:
            im = self.bpy.data.images.load(path, check_existing=True)
            im.colorspace_settings.name = 'sRGB' if colour else 'Non-Color'
            self.imgs[path] = im
        return self.imgs[path]

    def material(self, name, mode):
        key = (name, mode)
        if key in self.mats:
            return self.mats[key]
        rec = self.recs.get(name) or {}
        ins = next(iter((rec.get('nodes') or {'': {'inputs': {}}}).values()))['inputs']
        m = self.bpy.data.materials.new('%s_%s' % (name, mode))
        try:
            m.use_nodes = True
        except Exception:  # noqa: BLE001 - (always on in newer Blender)
            pass
        nt = m.node_tree
        for n in list(nt.nodes):
            nt.nodes.remove(n)
        L = nt.links.new
        out = nt.nodes.new('ShaderNodeOutputMaterial')
        em = nt.nodes.new('ShaderNodeEmission')
        em.inputs['Strength'].default_value = 1.0
        mix = nt.nodes.new('ShaderNodeMixShader')
        tr = nt.nodes.new('ShaderNodeBsdfTransparent')
        L(tr.outputs[0], mix.inputs[1])
        L(em.outputs[0], mix.inputs[2])
        L(mix.outputs[0], out.inputs['Surface'])

        def tex(path, colour):
            im = self.image(path, colour)
            if im is None:
                return None
            t = nt.nodes.new('ShaderNodeTexImage')
            t.image = im
            uvn = nt.nodes.new('ShaderNodeUVMap')
            uvn.uv_map = 'UV'
            L(uvn.outputs['UV'], t.inputs['Vector'])
            return t
        dt = tex((ins.get('Diffuse') or {}).get('image'), True)
        if dt is not None and 'Alpha' in ins:
            gt = nt.nodes.new('ShaderNodeMath')
            gt.operation = 'GREATER_THAN'
            gt.inputs[1].default_value = 0.5
            L(dt.outputs['Alpha'], gt.inputs[0])
            L(gt.outputs[0], mix.inputs[0])
        else:
            mix.inputs[0].default_value = 1.0
        if mode == 'colour':
            if dt is not None:
                L(dt.outputs['Color'], em.inputs['Color'])
            else:
                em.inputs['Color'].default_value = (0.2, 0.2, 0.2, 1.0)
        else:
            nt_ = tex((ins.get('Normal') or {}).get('image'), False)
            if nt_ is not None:
                sep = nt.nodes.new('ShaderNodeSeparateColor')
                comb = nt.nodes.new('ShaderNodeCombineColor')
                inv = nt.nodes.new('ShaderNodeMath')
                inv.operation = 'SUBTRACT'
                inv.inputs[0].default_value = 1.0
                L(nt_.outputs['Color'], sep.inputs[0])
                L(sep.outputs[0], comb.inputs[0])
                L(sep.outputs[1], inv.inputs[1])
                L(inv.outputs[0], comb.inputs[1])
                L(sep.outputs[2], comb.inputs[2])
                nm = nt.nodes.new('ShaderNodeNormalMap')
                nm.uv_map = 'UV'
                L(comb.outputs[0], nm.inputs['Color'])
                nrm = nm.outputs['Normal']
            else:
                nrm = nt.nodes.new('ShaderNodeNewGeometry').outputs['Normal']
            if mode == 'normal_up':
                # toward the viewer straight above whatever the winding (the renderer's two-sided rule: flipped where
                # it faces away from the eye; Blender flips by the winding, and the frond normals agree with theirs
                # on a third to a half of the triangles)
                xyz = nt.nodes.new('ShaderNodeSeparateXYZ')
                L(nrm, xyz.inputs[0])
                gt0 = nt.nodes.new('ShaderNodeMath')
                gt0.operation = 'GREATER_THAN'
                gt0.inputs[1].default_value = 0.0
                L(xyz.outputs['Z'], gt0.inputs[0])
                sg = nt.nodes.new('ShaderNodeMath')
                sg.operation = 'MULTIPLY_ADD'
                sg.inputs[1].default_value = 2.0
                sg.inputs[2].default_value = -1.0
                L(gt0.outputs[0], sg.inputs[0])
                sc = nt.nodes.new('ShaderNodeVectorMath')
                sc.operation = 'SCALE'
                L(nrm, sc.inputs[0])
                L(sg.outputs[0], sc.inputs['Scale'])
                nrm = sc.outputs[0]
            mul = nt.nodes.new('ShaderNodeVectorMath')
            mul.operation = 'MULTIPLY'
            mul.inputs[1].default_value = (0.5, -0.5, 0.5)
            add = nt.nodes.new('ShaderNodeVectorMath')
            add.operation = 'ADD'
            add.inputs[1].default_value = (0.5, 0.5, 0.5)
            L(nrm, mul.inputs[0])
            L(mul.outputs[0], add.inputs[0])
            L(add.outputs[0], em.inputs['Color'])
        self.mats[key] = m
        return m

    def build(self, parts, mode='colour'):
        bpy = self.bpy
        objs = []
        for pos, nrm, uv, tri, name, _cut in parts:
            me = bpy.data.meshes.new('lod0')
            me.from_pydata(pos.tolist(), [], tri.tolist())
            uvl = me.uv_layers.new(name='UV')
            loops = np.empty(len(me.loops), np.int64)
            me.loops.foreach_get('vertex_index', loops)
            u = np.asarray(uv, np.float64)[loops].copy()
            u[:, 1] = 1.0 - u[:, 1]                       # game v-down -> Blender v-up
            uvl.data.foreach_set('uv', u.astype(np.float32).ravel())
            me.normals_split_custom_set_from_vertices(np.asarray(nrm).tolist())
            me.materials.append(self.material(name, mode))
            o = bpy.data.objects.new('lod0', me)
            o['w3_material'] = name
            bpy.context.scene.collection.objects.link(o)
            objs.append(o)
        return objs

    def use(self, objs, mode):
        for o in objs:
            o.data.materials[0] = self.material(o['w3_material'], mode)

    def clear(self, objs):
        for o in objs:
            me = o.data
            self.bpy.data.objects.remove(o)
            self.bpy.data.meshes.remove(me)


def blender_render_setup(px_w, px_h, samples, raw):
    import bpy
    s = bpy.context.scene
    s.render.engine = 'BLENDER_EEVEE'
    s.render.film_transparent = True
    s.render.dither_intensity = 0.0
    s.eevee.taa_render_samples = samples
    s.render.resolution_x, s.render.resolution_y, s.render.resolution_percentage = px_w, px_h, 100
    s.render.image_settings.file_format = 'PNG'
    s.render.image_settings.color_mode = 'RGBA'
    s.render.image_settings.color_depth = '8'
    s.view_settings.view_transform = 'Raw' if raw else 'Standard'
    s.view_settings.look = 'None'
    s.view_settings.exposure = 0.0
    s.view_settings.gamma = 1.0


def blender_camera(name='w3cam'):
    import bpy
    s = bpy.context.scene
    if name in bpy.data.objects:
        cam = bpy.data.objects[name]
    else:
        cam = bpy.data.objects.new(name, bpy.data.cameras.new(name))
        s.collection.objects.link(cam)
    s.camera = cam
    cam.data.type = 'ORTHO'
    cam.data.clip_start, cam.data.clip_end = 0.01, 2000.0
    return cam


def png_read(path):
    """A PNG's pixels as stored (0..1, rows top-down, RGBA), in Blender."""
    import bpy
    im = bpy.data.images.load(path)
    im.colorspace_settings.name = 'Non-Color'
    w, h = im.size
    a = np.empty(w * h * 4, np.float32)
    im.pixels.foreach_get(a)
    bpy.data.images.remove(im)
    return a.reshape(h, w, 4)[::-1].copy()


def png_write(path, a):
    """RGBA 0..1 rows top-down, as stored (no colour transform), in Blender."""
    import bpy
    h, w = a.shape[:2]
    im = bpy.data.images.new('w3png', w, h, alpha=True)
    im.colorspace_settings.name = 'Non-Color'
    im.alpha_mode = 'STRAIGHT'
    im.pixels.foreach_set(np.ascontiguousarray(a[::-1], np.float32).ravel())
    im.filepath_raw = path
    im.file_format = 'PNG'
    im.save()
    bpy.data.images.remove(im)


def dilate(rgb, mask, steps=24):
    """Colour pushed out of the covered pixels into their uncovered neighbours (no dark fringe in the mips)."""
    rgb, m = rgb.copy(), mask.copy()
    for _ in range(steps):
        if m.all():
            break
        acc = np.zeros_like(rgb)
        cnt = np.zeros(m.shape, np.float32)
        for d in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            acc += np.roll(rgb * m[..., None], d, (0, 1))
            cnt += np.roll(m, d, (0, 1))
        new = (~m) & (cnt > 0)
        rgb[new] = acc[new] / cnt[new][:, None]
        m |= new
    if (~m).any():
        rgb[~m] = rgb[m].mean(0) if m.any() else 0.5
    return rgb


def bake_tops(work, uncook, only=None):
    """In Blender (--bake-tops): each round tree's top image from its LOD 0 glb (contract: orthographic, straight down,
    transparent background, square): <work>/srt_tops/<key>_c.png (the diffuse as stored, unlit; alpha the coverage of
    the cutouts at 0.5, 16 samples) and <key>_n.png (the shading normal with the leaf normal maps, as a DirectX map in
    the top card's axes: x +x, y +y (green inverted, as the game's), z up; alpha 0.9, a roughness for the contract; the
    scene builder reads only rgb). Colour and normals pushed into the uncovered pixels. The renders in raw/; tops.json:
    per tree its images and square (top_frame). Flat trees (fixed planes) get none."""
    import bpy
    from mathutils import Euler
    out = os.path.join(work, TOPS)
    os.makedirs(os.path.join(out, 'raw'), exist_ok=True)
    index = json.load(open(os.path.join(work, 'meshes', 'index.json'), encoding='utf-8'))
    materials = json.load(open(os.path.join(work, 'materials.json'), encoding='utf-8'))
    tj = os.path.join(out, 'tops.json')
    tops = json.load(open(tj, encoding='utf-8')) if os.path.exists(tj) else {'trees': {}}
    bpy.ops.wm.read_homefile(use_empty=True)
    bt = BlenderTree(materials)
    cam = blender_camera()
    cam.rotation_euler = Euler((0.0, 0.0, 0.0))       # looking down -Z, the image's up +Y
    done = 0
    for path in sorted(index):
        rec = index[path]
        stem = path.rsplit(BS, 1)[-1].rsplit('.', 1)[0]
        if rec.get('kind') != 'srt' or (only and stem not in only):
            continue
        js = uncook_file(uncook, path) + '.json'
        if not os.path.exists(js):
            continue
        srt = json.load(open(js, encoding='utf-8'))
        bb = billboard_views(srt)
        if not bb:
            continue
        _, _, flat = billboard_planes(bb, np.concatenate([p[0][:, :2] for p in lod0_parts(work, rec, materials)]),
                                      wall_plant(path))
        if flat:
            continue
        parts = lod0_parts(work, rec, materials, srt, np.eye(3))      # (facing leaves toward the camera above)
        x0, y0, side, z = top_frame([(p[0], p[5]) for p in parts])
        px = TOP_PX if side >= TOP_SMALL else TOP_PX // 2
        objs = bt.build(parts, 'colour')
        zmax = max(float(p[0][:, 2].max()) for p in parts)
        cam.location = (x0 + side / 2, y0 + side / 2, zmax + 20.0)
        cam.data.ortho_scale = side
        key = mesh_key(path)
        raw = {}
        for mode in ('colour', 'normal_up'):
            bt.use(objs, mode)
            blender_render_setup(px, px, 16, mode != 'colour')
            raw[mode] = os.path.join(out, 'raw', '%s_%s.png' % (key, mode[0]))
            bpy.context.scene.render.filepath = raw[mode]
            bpy.ops.render.render(write_still=True)
        bt.clear(objs)
        c, n = png_read(raw['colour']), png_read(raw['normal_up'])
        cov = c[..., 3]
        m = cov > 0.01
        col = np.dstack([dilate(c[..., :3], m), cov])
        v = n[..., :3] * 2.0 - 1.0
        v /= np.maximum(np.linalg.norm(v, axis=-1, keepdims=True), 1e-6)
        v = dilate(v, m)
        v /= np.maximum(np.linalg.norm(v, axis=-1, keepdims=True), 1e-6)
        nrm = np.dstack([v * 0.5 + 0.5, np.full(cov.shape, 0.9, np.float32)])
        png_write(os.path.join(out, key + '_c.png'), col)
        png_write(os.path.join(out, key + '_n.png'), nrm)
        tops['trees'][path] = {'colour': key + '_c.png', 'normal': key + '_n.png', 'x0': x0, 'y0': y0, 'side': side,
                               'z': z, 'px': px, 'coverage': round(float(cov.mean()), 4),
                               'normal_up_mean': round(float(v[..., 2][m].mean()), 4) if m.any() else None}
        done += 1
        print('W3TREES top', stem, px, 'side %.2f z %.2f coverage %.3f' % (side, z, cov.mean()), flush=True)
    with open(tj, 'w', encoding='utf-8') as f:
        json.dump(tops, f, indent=1)
    print('W3TREES tops baked', done, '->', tj, flush=True)


# ---------------------------------------------------------------------------------------------------- conversion

def lod_distances(profile, n):
    hi = float(profile.get('m_fHighDetail3dDistance', 0.0))
    lo = float(profile.get('m_fLowDetail3dDistance', hi))
    if n <= 1:
        return [0.0]
    if n == 2:
        return [0.0, hi]
    return [0.0] + [hi + (lo - hi) * (k - 1) / (n - 2) for k in range(1, n)]


BB_FLAT = 0.4       # LOD 0's horizontal spread, minor over major axis, under which a tree gets two axis planes
BB_GAIN = 2         # a billboard is added only where it has at most 1 / BB_GAIN of the last 3D LOD's triangles
BB_MIN = 32         # and where that LOD has at least this many (the city's vineyards, 15: as the crossed planes left
                    # them, their two extra draws and records for a few triangles; the smallest billboarded tree has 35)


def billboard_distance(profile, last):
    """The game's m_fBillboardStartDistance (its fade to the billboard starts there), never before the last 3D LOD."""
    d = float(profile.get('m_fBillboardStartDistance') or 0.0)
    return max(d, last + 1.0)


def wall_plant(srt_path):
    """A plant grown on a wall (the stem says so: vine_wall_*, rose_*_wall_*): fixed planes whatever LOD 0's spread (the
    slim ones, 1.2-1.7 m wide, spread 0.58-0.62: a turning card would swing into the wall)."""
    return '_wall' in srt_path.rsplit(BS, 1)[-1].lower()


def billboard_planes(bb, xy, fixed=None):
    """The billboard's planes, [(view, azimuth of the plane's front (rad), width scale)], and LOD 0's horizontal
    spread (minor / major). View i is the tree seen from azimuth 2 pi i / n in mesh space (+X = 0, counter-clockwise),
    unmirrored on a plane facing there (checked against LOD 0's silhouettes); the plane's back shows it
    mirrored, which is exactly the opposite view's silhouette.
    - Round trees: n / 2 planes for an even n (views 0 .. n/2 - 1: each plane both a view and its opposite), all n for
      an odd n.
    - Flat ones (wall vines and roses, hedges, lying branches): two planes along LOD 0's principal axes, each showing
      the view nearest its axis stretched by 1 / cos(the angle between them), so a flat tree's broadside lies on its
      own plane instead of a star of fins standing out of the wall. fixed: flat whatever the spread (wall_plant).
    Returns (planes, spread, flat)."""
    n = len(bb['views'])
    az = [2 * np.pi * i / n for i in range(n)]
    c = xy - xy.mean(0)
    ev, evec = np.linalg.eigh(np.cov(c.T)) if len(c) > 2 else (np.ones(2), np.eye(2))
    spread = float(np.sqrt(max(ev[0], 0.0) / max(ev[1], 1e-12)))
    flat = bool(fixed) or spread < BB_FLAT
    if not flat:
        return [(i, az[i], 1.0) for i in (range(n // 2) if n % 2 == 0 else range(n))], spread, flat
    out = []
    for axis in (evec[:, 0], evec[:, 1]):       # the broadside (facing the minor axis), then the end
        a = float(np.arctan2(axis[1], axis[0]))
        cos = [abs(np.cos(t - a)) for t in az]
        i = int(np.argmax(cos))
        out.append((i, a if np.cos(az[i] - a) > 0 else a + np.pi, 1.0 / max(cos[i], 0.5)))
    return out, spread, flat


def billboard_mesh(bb, planes):
    """The planes as one primitive: each the billboard's cutout polygon (the add-on's Billboard_Cutout: its unit
    square across the width and bottom..top), UVs from the view's atlas frame (rotated frames as io_mesh_srt maps them,
    the JSON's v as stored, as the 3D LODs)."""
    cv, ct = bb['cutout']
    W, top, bot = bb['width'], bb['top'], bb['bottom']
    P, N, UV, T = [], [], [], []
    nv = 0
    for i, front, scale in planes:
        u, v, du, dv, rot = bb['views'][i]
        nf = np.array([np.cos(front), np.sin(front), 0.0])
        r = np.array([-np.sin(front), np.cos(front), 0.0])
        pos = ((cv[:, 0] - 0.5) * W * scale)[:, None] * r
        pos[:, 2] = bot + (top - bot) * cv[:, 1]
        if rot:
            uv = np.stack([u + du * cv[:, 1], v + dv * cv[:, 0]], -1)
        else:
            uv = np.stack([u + du * cv[:, 0], v + dv * cv[:, 1]], -1)
        tri = ct.copy()
        e = np.cross(pos[tri[:, 1]] - pos[tri[:, 0]], pos[tri[:, 2]] - pos[tri[:, 0]]) @ nf
        tri[e < 0] = tri[e < 0][:, ::-1]        # counter-clockwise about the front, as the bark's winding
        P.append(pos)
        N.append(np.repeat(nf[None], len(pos), 0))
        UV.append(uv)
        T.append(tri + nv)
        nv += len(pos)
    return np.concatenate(P), np.concatenate(N), np.concatenate(UV).astype(np.float32), np.concatenate(T)


BB_A0 = 0.0         # view 0's azimuth (rad, mesh +X = 0, counter-clockwise): the viewer stands at (cos a, sin a, 0)


def view_maps(bb):
    """Per view the map from the card's (s, t) (s left to right as the viewer sees it, t bottom to top) to the atlas UV
    (as the 3D LODs': the JSON's v as stored): u = m0 s + m1 t + m2, v = m3 s + m4 t + m5. A frame packed rotated holds
    the card's s along its v and t along its u (as io_mesh_srt maps the cutout); a negative du / dv is a mirrored or
    flipped packing (the city's: rotated frames du, dv < 0, the others dv < 0)."""
    out = []
    for u, v, du, dv, rot in bb['views']:
        out.append([0.0, du, u, dv, 0.0, v] if rot else [du, 0.0, u, 0.0, dv, v])
    return out


def card_mesh(bb):
    """The vertical card (contract part 1): the cutout polygon at (0, x, z) in mesh space, x = (s - 0.5) width (+ to
    the viewer's right), z from FBottomPos to FTopPos; UV (s, t); normal +X (the renderer replaces it). Facing +X
    unturned (view 0 for BB_A0 0), wound counter-clockwise about it."""
    cv, ct = bb['cutout']
    W, top, bot = bb['width'], bb['top'], bb['bottom']
    pos = np.stack([np.zeros(len(cv)), (cv[:, 0] - 0.5) * W, bot + (top - bot) * cv[:, 1]], -1)
    tri = ct.copy()
    e = np.cross(pos[tri[:, 1]] - pos[tri[:, 0]], pos[tri[:, 2]] - pos[tri[:, 0]])[:, 0]
    tri[e < 0] = tri[e < 0][:, ::-1]
    return pos, np.tile([1.0, 0.0, 0.0], (len(pos), 1)), cv.astype(np.float32), tri


TOP_PX = 512        # the baked top image's side; canopies under TOP_SMALL m across: TOP_PX / 2
TOP_SMALL = 4.0
TOP_SPARE = 1.02    # the square's side over the canopy's larger extent


def top_frame(parts):
    """The top card's square: [(positions, cutout?)] of LOD 0 -> (x0, y0, side, z): LOD 0's x/y extent (centred, square,
    TOP_SPARE), at the canopy's mid height (the cutout parts' z range: leaves and fronds; every part's where none)."""
    P = np.concatenate([p for p, _ in parts])
    C = [p for p, c in parts if c]
    Z = np.concatenate(C)[:, 2] if C else P[:, 2]
    lo, hi = P[:, :2].min(0), P[:, :2].max(0)
    side = float(max(hi - lo)) * TOP_SPARE
    c = (lo + hi) / 2
    return float(c[0] - side / 2), float(c[1] - side / 2), side, float((Z.min() + Z.max()) / 2)


def top_mesh(top):
    """The top card (contract part 2): the square at its height, UV over the baked image (glTF: u along +x, v down the
    image, the image's top row at +y, as the bake's camera sees it), normal +z, wound counter-clockwise about it."""
    x0, y0, s, z = top['x0'], top['y0'], top['side'], top['z']
    pos = np.array([[x0, y0, z], [x0 + s, y0, z], [x0 + s, y0 + s, z], [x0, y0 + s, z]])
    uv = np.array([[0.0, 1.0], [1.0, 1.0], [1.0, 0.0], [0.0, 0.0]], np.float32)
    return pos, np.tile([0.0, 0.0, 1.0], (4, 1)), uv, np.array([[0, 1, 2], [0, 2, 3]], np.int64)


def frame_axes(view):
    """The image axes (right, up; v grows down) of a view's billboard right and up: 2 x 2, columns right, up."""
    u, v, du, dv, rot = view
    su, sv = (1.0 if du >= 0 else -1.0), (1.0 if dv >= 0 else -1.0)
    if rot:
        return np.array([[0.0, su], [-sv, 0.0]])
    return np.array([[su, 0.0], [0.0, -sv]])


class BillboardNormals:
    """The billboard normal atlases rewritten for w3scene. SpeedTree stores a billboard's normals in its own frame (x the
    billboard's right, y its up: green up), and a frame packed rotated in the atlas keeps those values (in
    the city's 23 atlases the normals' x follows the image's up in every rotated frame, its right in every other).
    w3scene reads a normal map as DirectX (green down) in the image's axes. Each frame's (x, y) is turned into the
    image's axes and green inverted; written under <images>/_bbn/ as PNG (lossless; the scene build compresses).
    For the flat trees' fixed planes (static geometry: w3scene's frame from the UVs); the turning cards read CardNormals."""

    def __init__(self, img_root):
        self.img_root = img_root
        self.frames = OrderedDict()     # source -> {pixel rect (fractions rounded): 2 x 2}
        self.conflicts = 0

    def path(self, src):
        stem = os.path.splitext(os.path.basename(src))[0]
        return os.path.join(self.img_root, '_bbn', stem + '_' + hashlib.sha1(src.lower().encode('utf-8')).hexdigest()[:8]
                            + '.png')

    def add(self, src, views):
        f = self.frames.setdefault(src, {})
        for view in views:
            u, v, du, dv, _ = view
            rect = tuple(round(x, 4) for x in (min(u, u + du), max(u, u + du), min(v, v + dv), max(v, v + dv)))
            m = frame_axes(view)
            if rect in f and not np.array_equal(f[rect], m):
                self.conflicts += 1
                continue
            f[rect] = m

    def write(self, missing):
        from PIL import Image
        for src, frames in self.frames.items():
            if not os.path.exists(src):
                missing.add(src)
                continue
            im = np.asarray(Image.open(src).convert('RGBA'))
            H, W = im.shape[:2]
            xy = im[..., :2].astype(np.float32) / 127.5 - 1.0
            out = xy.copy()
            for (u0, u1, v0, v1), m in frames.items():
                c0, c1, r0, r1 = int(round(u0 * W)), int(round(u1 * W)), int(round(v0 * H)), int(round(v1 * H))
                out[r0:r1, c0:c1] = xy[r0:r1, c0:c1] @ m.T
            out[..., 1] *= -1.0
            res = im.copy()
            res[..., :2] = np.uint8(np.clip(np.round(out * 127.5 + 127.5), 0, 255))
            dst = self.path(src)
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            Image.fromarray(res).save(dst)


class CardNormals:
    """The billboard normal atlases for the turning cards (contract: the renderer's tangent is the card's right, its
    bitangent the card's up, for every view). SpeedTree's atlas already holds each frame's normals in the billboard's
    own axes (x its right, y its up, green up; check_views: in every view of the city's round trees, x grows to the
    viewer's right and y upward, rotated and flipped frames alike), so only green is inverted: w3scene reads every
    normal map as DirectX and turns it to OpenGL. Written under <images>/_bbc/ as PNG, alpha kept."""

    def __init__(self, img_root):
        self.img_root = img_root
        self.sources = OrderedDict()

    def path(self, src):
        stem = os.path.splitext(os.path.basename(src))[0]
        return os.path.join(self.img_root, '_bbc', stem + '_' + hashlib.sha1(src.lower().encode('utf-8')).hexdigest()[:8]
                            + '.png')

    def add(self, src):
        self.sources[src] = True
        return self.path(src)

    def write(self, missing):
        from PIL import Image
        for src in self.sources:
            if not os.path.exists(src):
                missing.add(src)
                continue
            res = np.asarray(Image.open(src).convert('RGBA')).copy()
            res[..., 1] = 255 - res[..., 1]
            dst = self.path(src)
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            Image.fromarray(res).save(dst)


def mat_key(rec):
    """A SpeedTree record's sharing key: (diffuse, normal, cutout, billboard part, its card record). An earlier run's
    billboard records (billboard true: the crossed planes) match none of today's."""
    ins = next(iter(rec['nodes'].values()))['inputs']
    card = rec.get('card')
    return ((ins.get('Diffuse') or {}).get('image', '').lower(), (ins.get('Normal') or {}).get('image', '').lower(),
            rec.get('blend_method') == 'CLIP', rec.get('billboard') or False,
            json.dumps(card, sort_keys=True) if card else '')


class Materials:
    """materials.json records for the SpeedTree render states, shared across trees by (diffuse, normal, cutout,
    billboard). With reuse, a record already in materials.json with the same key is used (update: an earlier run's, the
    name in `prefer` (the tree's slots before) where there are several, so placements keep their names)."""

    def __init__(self, existing, reuse=False):
        self.recs = existing
        self.made = OrderedDict()
        self.by_key = {}
        self.prefer = ()
        for name, rec in existing.items() if reuse else ():
            if rec.get('speedtree'):
                self.by_key.setdefault(mat_key(rec), []).append(name)

    def name(self, srt_path, folder, rs, missing, img_root=None, part=None, normal=None, card=None, images=None):
        """A render state's record; with part a billboard's: 'card' (the vertical card: normal the CardNormals atlas,
        card its .xw3b record), 'fixed' (a flat tree's planes: normal the BillboardNormals atlas) or 'top' (images the
        baked (colour, normal))."""
        billboard = part or False
        tex = rs['ApTextures'] if rs else ['']
        if part == 'top':
            diff, nrm = images
        else:
            diff = find_image(folder, tex[0], img_root, srt_path) if tex[0] else ''
        if part in ('card', 'fixed'):
            nrm = normal or ''
        elif not part:
            nrm = find_image(folder, tex[1], img_root, srt_path) if len(tex) > 1 and tex[1] else ''
        # (a card's or plane's normal: written from its source by CardNormals / BillboardNormals.write)
        for p in (diff, nrm if billboard in (False, 'top') else ''):
            if p and not os.path.exists(p):
                missing.add(p)
        cut = bool(billboard) or not rs.get('BDiffuseAlphaMaskIsOpaque', False)
        key = (diff.lower(), nrm.lower(), cut, billboard, json.dumps(card, sort_keys=True) if card else '')
        if key in self.by_key:
            names = self.by_key[key]
            return next((n for n in names if n in self.prefer), names[0])
        stem = lambda p: os.path.splitext(os.path.basename(p))[0] if p else 'none'  # noqa: E731
        tree = srt_path.rsplit(BS, 1)[-1].rsplit('.', 1)[0]
        if part == 'card':
            name = 'srtbb_' + tree          # (not srt_: no canopy shading, see the docstring)
        elif part == 'top':
            name = 'srttop_' + tree
        elif part == 'fixed':
            name = 'srtbbf_' + stem(diff)
        else:
            name = 'srt_' + stem(diff)
            if nrm and stem(nrm) != stem(diff) + '_n':
                name += '_' + stem(nrm)
            if cut:
                name += '_cut'
        base, k = name, 2
        while name in self.recs or name in self.made:
            name = '%s_%d' % (base, k)
            k += 1

        def img(p, out):
            return {'node': 'TEX_IMAGE', 'name': 'Diffuse' if out == 'Alpha' or p == diff else 'Normal', 'out': out,
                    'image': p, 'colorspace': 'sRGB' if p == diff else 'Non-Color', 'packed': False,
                    'extension': 'REPEAT'}
        inputs = {}
        if diff:
            inputs['Diffuse'] = img(diff, 'Color')
            if cut:
                inputs['Alpha'] = img(diff, 'Alpha')
        if nrm:
            inputs['Normal'] = img(nrm, 'Color')
        params = {k: (rs or {}).get(k) for k in GEOM + ('BDiffuseAlphaMaskIsOpaque', 'FAlphaScalar', 'EFaceCulling',
                                                        'BUsedAsGrass', 'EDetailLayer', 'ESpecular', 'FShininess',
                                                        'FDiffuseScalar', 'EHueVariation')}
        params['ApTextures'] = list(tex)
        rec = {'base': srt_path, 'params': params, 'source': 'speedtree', 'source_mesh': srt_path,
               'groups': ['Witcher3_Main'], 'blend_method': 'CLIP' if cut else 'OPAQUE',
               'nodes': {'speedtree:' + name: {'tree': 'Witcher3_Main', 'inputs': inputs}},
               'images': [], 'speedtree': True}
        if billboard:
            rec['billboard'] = part
            if part != 'top':
                rec['source_normal'] = find_image(folder, tex[1], img_root, srt_path) if len(tex) > 1 and tex[1] else ''
            if card:
                rec['card'] = card
        self.made[name] = rec
        self.by_key[key] = [name]
        return name


def convert(srt_path, srt, folder, mats, missing, img_root=None, bbn=None, bbc=None, tops=None):
    """One SRT JSON into glb primitives (with bbn and bbc, the billboard as a last LOD; tops: tops.json's entries).
    Returns (prims, slots, lods, stats)."""
    g = srt['Geometry']
    plods = g['PLods']
    states = g['P3dRenderStateMain']
    grass = any(r.get('BUsedAsGrass') for r in states)
    if grass:
        plods = plods[:1]
    profile = srt.get('LodProfile', {})
    dist = lod_distances(profile, len(plods))
    prims, slots, lods = [], [], []
    st = Counter()
    for li, lod in enumerate(plods):
        L = {'lod': li, 'vertices': 0, 'triangles': 0, 'distance': dist[li]}
        calls = lod['PDrawCalls'][:1] if grass else lod['PDrawCalls']
        for dc in calls:
            rs = states[dc['RenderStateIdx']]
            vd = dc['VertexData']
            pos = np.array(vd['pos'], dtype=np.float64).reshape(-1, 3)
            if len(pos) == 0:
                continue
            if vd.get('leaf_card_corner'):
                # the card centre plus its corner, (z, x, y) as io_mesh_srt stores it: the Leaf_Card node without a
                # camera (fixed cards)
                pos = pos + np.array(vd['leaf_card_corner'], dtype=np.float64).reshape(-1, 3)[:, [2, 0, 1]]
                st['facing_leaf_vertices'] += len(pos)
            nrm = np.array(vd['normals'], dtype=np.float64).reshape(-1, 3) / 255.0 * 2.0 - 1.0
            ln = np.linalg.norm(nrm, axis=1, keepdims=True)
            nrm = np.where(ln > 1e-6, nrm / np.maximum(ln, 1e-6), np.array([0.0, 0.0, 1.0]))
            uv = np.array(vd['diffuse'], dtype=np.float64).reshape(-1, 2)
            tri = np.array(dc['IndexData'], dtype=np.int64).reshape(-1, 3)
            if len(tri) == 0:
                continue
            name = mats.name(srt_path, folder, rs, missing, img_root)
            if name not in slots:
                slots.append(name)
            prims.append({'position': pos, 'normal': nrm, 'uv': uv.astype(np.float32), 'indices': tri,
                          'extras': {'lod': li, 'distance': dist[li], 'material': slots.index(name),
                                     'speedtree': [k for k in GEOM if rs.get(k)]}})
            L['vertices'] += len(pos)
            L['triangles'] += len(tri)
            st['draw_calls'] += 1
        if L['triangles']:
            lods.append(L)
    bb = billboard_views(srt) if bbn is not None and lods else None
    if bb:
        st['billboard_data'] = 1
        xy = np.concatenate([p['position'][:, :2] for p in prims if p['extras']['lod'] == lods[0]['lod']])
        planes, spread, flat = billboard_planes(bb, xy, wall_plant(srt_path))
        tex = bb['state']['ApTextures']
        src = find_image(folder, tex[1], img_root, srt_path) if len(tex) > 1 and tex[1] else ''
        top = (tops or {}).get(srt_path)
        if flat:
            # fixed planes (a vine on a wall must not turn to face the viewer), no top card, no .xw3b record
            parts = [('fixed', billboard_mesh(bb, planes), dict(normal=bbn.path(src) if src else ''),
                      {'part': 'fixed', 'views': [p[0] for p in planes], 'spread': round(spread, 3)})]
        else:
            n = len(bb['views'])
            card = {'type': 1, 'views': n, 'a0': BB_A0, 'uv': [[round(float(x), 7) for x in m] for m in view_maps(bb)]}
            parts = [('card', card_mesh(bb), dict(normal=bbc.add(src) if src else '', card=card),
                      {'part': 'card', 'views': n, 'spread': round(spread, 3)})]
            if top is None:
                missing.add('top image of %s (run --bake-tops in Blender)' % srt_path)
            else:
                parts.append(('top', top_mesh(top), dict(images=(top['colour'], top['normal']), card={'type': 2}),
                              {'part': 'top', 'side': round(top['side'], 3), 'z': round(top['z'], 3)}))
        last = lods[-1]
        ntri = sum(len(g[3]) for _, g, _, _ in parts)
        if ntri * BB_GAIN > last['triangles'] or last['triangles'] < BB_MIN:
            st['billboard_not_cheaper'] = 1     # (the last 3D LOD is as cheap: kept to the end, as before)
        else:
            li, d = last['lod'] + 1, billboard_distance(profile, last['distance'])
            L = {'lod': li, 'vertices': 0, 'triangles': 0, 'distance': d, 'billboard': True, 'flat': flat,
                 'parts': [p[0] for p in parts], 'planes': len(planes) if flat else 0}
            for part, (pos, nrm, uv, tri), kw, info in parts:
                name = mats.name(srt_path, folder, None if part == 'top' else bb['state'], missing, img_root, part,
                                 **kw)
                if name not in slots:
                    slots.append(name)
                prims.append({'position': pos, 'normal': nrm, 'uv': uv, 'indices': tri,
                              'extras': {'lod': li, 'distance': d, 'material': slots.index(name),
                                         'speedtree': ['billboard'], 'billboard': info}})
                L['vertices'] += len(pos)
                L['triangles'] += len(tri)
            lods.append(L)
            st['billboard'] = 1
            st['billboard_flat'] = int(flat)
            st['billboard_top'] = int('top' in L['parts'])
    return prims, slots, lods, st


def build(paths, uncook, mats, img_root, missing, prefer=None, tops=None):
    """Every tree's primitives: {srt path: (prims, slots, lods, stats, srt)}, {srt path: error}, the normal atlases.
    prefer: {srt path: its material names before} (Materials.prefer); tops: tops.json's trees."""
    srts = OrderedDict()
    bbn = BillboardNormals(img_root)
    bbc = CardNormals(img_root)
    for path in paths:
        ab = uncook_file(uncook, path)
        js = ab + '.json'
        if not os.path.exists(js):
            missing.add(js)
            continue
        srt = json.load(open(js, encoding='utf-8'))
        folder = os.path.dirname(ab)
        srts[path] = (srt, folder)
        bb = billboard_views(srt)
        tex = bb['state']['ApTextures'] if bb else []
        if len(tex) > 1 and tex[1]:
            bbn.add(find_image(folder, tex[1], img_root, path), bb['views'])
    done, errors = OrderedDict(), {}
    for path, (srt, folder) in srts.items():
        mats.prefer = set((prefer or {}).get(path) or ())
        prims, slots, lods, st = convert(path, srt, folder, mats, missing, img_root, bbn, bbc, tops)
        if not prims:
            errors[path] = {'error': 'no geometry'}
            continue
        done[path] = (prims, slots, lods, st, srt)
    if not missing:
        bbn.write(missing)
        bbc.write(missing)
    return done, errors, bbn


def tree_paths(placements):
    """The trees: SpeedTree placements, and those an earlier run (or a district merged in) already rewrote."""
    return sorted({p['mesh'] for p in placements if p['kind'] == 'srt' or p.get('srt_source')})


def write(work, uncook, img_root=None, update=False, backup='pre-update', rebase=False):
    """Converts every tree and writes the outputs. Default: from the *.pre-trees.json copies (made on the first run),
    replacing the trees of an earlier run. update: in place on the current placements, materials and index (later steps'
    entries kept); only the trees' entries, glbs and placements' slots change, each file first copied once to
    *.<backup>.json (the glbs to meshes/<backup>/)."""
    img_root = img_root or os.path.join(work, 'srt_images')
    idx = os.path.join('meshes', 'index.json')
    if update:
        base = {'placements.json': 'placements.json', 'materials.json': 'materials.json', idx: idx}
        for cur in list(base) + ['trees.json']:
            bak = os.path.splitext(cur)[0] + '.%s.json' % backup
            if os.path.exists(os.path.join(work, cur)) and not os.path.exists(os.path.join(work, bak)):
                shutil.copy2(os.path.join(work, cur), os.path.join(work, bak))
    else:
        pre = w3work.start(work, 'trees', rebase=rebase)
        base = {'placements.json': pre['placements'], 'materials.json': pre['materials'], idx: pre['index']}
        base = {k: os.path.relpath(v, work) for k, v in base.items()}
    pl = json.load(open(os.path.join(work, base['placements.json']), encoding='utf-8'))
    materials = json.load(open(os.path.join(work, base['materials.json']), encoding='utf-8'))
    index = json.load(open(os.path.join(work, base[idx]), encoding='utf-8'))

    paths = tree_paths(pl['placements'])
    mats = Materials(materials, reuse=update)
    missing = set()
    tj = os.path.join(work, TOPS, 'tops.json')
    tops = json.load(open(tj, encoding='utf-8'))['trees'] if os.path.exists(tj) else {}
    tops = {p: dict(t, colour=os.path.join(work, TOPS, t['colour']), normal=os.path.join(work, TOPS, t['normal']))
            for p, t in tops.items()}
    done, trees, bbn = build(paths, uncook, mats, img_root, missing,
                             {p: (index.get(p) or {}).get('materials') for p in paths} if update else None, tops)
    if missing:
        print('W3TREES missing (run in Blender to materialize them):')
        for m in sorted(missing):
            print('   ', m)
        raise SystemExit(2)

    if update:
        keep = os.path.join(work, 'meshes', backup)
        os.makedirs(keep, exist_ok=True)
        for path in done:
            f = (index.get(path) or {}).get('file')
            if f and os.path.exists(os.path.join(work, 'meshes', f)) and not os.path.exists(os.path.join(keep, f)):
                shutil.copy2(os.path.join(work, 'meshes', f), os.path.join(keep, f))
    tri_total = Counter()
    for path, (prims, slots, lods, st, srt) in done.items():
        key = mesh_key(path)
        w = gltfio.Writer()
        w.mesh(key, prims)
        w.node(key, mesh=0)
        w.save(os.path.join(work, 'meshes', key + '.glb'),
               extras={'source': path, 'space': 'witcher3 mesh-local, metres, +Z up', 'materials': slots,
                       'speedtree': {'lod_profile': srt.get('LodProfile'), 'extents': srt.get('Extents'),
                                     'leaf_cards': 'fixed: centre + corner (z, x, y), facing +X'}})
        index[path] = {'file': key + '.glb', 'materials': slots, 'lods': lods, 'kind': 'srt'}
        trees[path] = {'file': key + '.glb', 'materials': slots, 'lods': lods, **st}
        tri_total['lod0'] += lods[0]['triangles']
        tri_total['all'] += sum(L['triangles'] for L in lods)

    materials.update(mats.made)
    out_pl, n_rw, n_slots, n_placed_tri, n_bb = [], 0, 0, 0, 0
    for p in pl['placements']:
        if p['mesh'] in done and (p['kind'] == 'srt' or p.get('srt_source')):
            q = dict(p)
            n_rw += p['kind'] == 'srt'
            n_slots += p['kind'] != 'srt'
            q['kind'] = 'w2mesh'
            q['slots'] = list(index[p['mesh']]['materials'])
            old = p.get('slots') or []
            if p['kind'] != 'srt' and len(old) <= len(q['slots']) and all(
                    a == b or (a in materials and b in materials and materials[a].get('speedtree')
                               and mat_key(materials[a]) == mat_key(materials[b])) for a, b in zip(old, q['slots'])):
                q['slots'][:len(old)] = old     # (its own names where they are the same record: a district's trees)
            q['expected_materials'] = len(q['slots'])
            q['srt_source'] = True
            out_pl.append(q)
            n_placed_tri += index[p['mesh']]['lods'][0]['triangles']
            n_bb += bool(index[p['mesh']]['lods'][-1].get('billboard'))
        else:
            out_pl.append(p)
    pl['placements'] = out_pl

    def dump(name, obj):
        with open(os.path.join(work, name), 'w', encoding='utf-8') as f:
            json.dump(obj, f, indent=0)
    dump('placements.json', pl)
    dump('materials.json', materials)
    dump(idx, index)
    w3work.wrote(work, 'trees')
    images = sorted({i['image'] for r in mats.made.values() for nd in r['nodes'].values()
                     for i in nd['inputs'].values() if isinstance(i, dict) and i.get('image')})
    T = [t for t in trees.values() if 'lods' in t]
    summary = {'mode': 'update' if update else 'pre-trees', 'placements_rewritten': n_rw,
               'placements_slots_updated': n_slots,
               'placements_srt_left': sum(1 for p in out_pl if p['kind'] == 'srt'),
               'unique_srt': len(done), 'triangles_lod0_unique': tri_total['lod0'],
               'triangles_all_lods_unique': tri_total['all'], 'triangles_lod0_placed': n_placed_tri,
               'billboard_data': sum(t.get('billboard_data', 0) for t in T),
               'billboards': sum(t.get('billboard', 0) for t in T),
               'billboards_flat': sum(t.get('billboard_flat', 0) for t in T),
               'billboards_top': sum(t.get('billboard_top', 0) for t in T),
               'billboards_not_cheaper': sum(t.get('billboard_not_cheaper', 0) for t in T),
               'billboard_placements': n_bb, 'billboard_normal_atlases': len(bbn.frames),
               'billboard_frame_conflicts': bbn.conflicts,
               'materials': len(mats.made), 'materials_cutout': sum(1 for r in mats.made.values()
                                                                   if r['blend_method'] == 'CLIP'),
               'images': len(images), 'image_files': images, 'trees': trees}
    dump('trees.json', summary)
    print('W3TREES', json.dumps({k: v for k, v in summary.items() if k not in ('trees', 'image_files')}), flush=True)
    return summary


def main():
    in_blender = 'bpy' in sys.modules or os.path.basename(sys.executable).lower().startswith('blender')
    argv = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else sys.argv[1:]
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--work', required=True)
    ap.add_argument('--w3', help='a research folder whose uncook/ is the add-on\'s uncook folder (default '
                    '$W3_UNCOOK, else $W3_RESEARCH/uncook; see w3paths.py)')
    ap.add_argument('--images', help='where images the uncook folder lacks go (default <work>/srt_images)')
    ap.add_argument('--update', action='store_true',
                    help='in place on the current placements/materials/index: only the trees change')
    ap.add_argument('--backup', default='pre-update', help='update: the backups\' name (*.NAME.json, meshes/NAME/)')
    ap.add_argument('--rebase', action='store_true', help='start from the current files when the pre-trees copies are '
                                                          'stale (w3work.py)')
    ap.add_argument('--no-prepare', action='store_true', help='in Blender: skip materializing')
    ap.add_argument('--bake-tops', action='store_true', help="in Blender: bake the top images from the current LOD 0 "
                                                              "glbs into <work>/srt_tops (nothing else)")
    ap.add_argument('--trees', help='bake-tops: only these .srt stems (comma-separated)')
    a = ap.parse_args(argv)
    img_root = a.images or os.path.join(a.work, 'srt_images')
    uncook = w3paths.uncook(a.w3)
    if a.bake_tops:
        if not in_blender:
            raise SystemExit('--bake-tops runs in Blender')
        bake_tops(a.work, uncook, a.trees.split(',') if a.trees else None)
        return
    if in_blender and not a.no_prepare:
        bak = os.path.join(a.work, 'placements.pre-trees.json')
        src = bak if os.path.exists(bak) and not a.update else os.path.join(a.work, 'placements.json')
        pl = json.load(open(src, encoding='utf-8'))
        prepare(tree_paths(pl['placements']), img_root)
    write(a.work, uncook, img_root, a.update, a.backup, a.rebase)


if __name__ == '__main__':
    main()
