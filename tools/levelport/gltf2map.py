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
"""Compile a levelport glTF into a Jedi Academy map: .map + misc_model chunks + shaders + textures -> q3map2 -> pk3.

    python tools/levelport/gltf2map.py <in.glb> <mapname> [--out DIR] [--preset quakelive] [--source-base DIR]
                                       [--chunk 1024] [--light fast|full|none] [--no-compile]

Node roles (extras "lp", README.md):
- render     triangles -> glTF (.glb) chunks on a --chunk grid -> lightmapped misc_model (spawnflags 4), or vertex-lit
             with --vertexlit. Material name = shader. COLOR_0 alpha rides into the BSP as vertex alpha.
- collision  one convex brush per node (planes from its faces, convex hull if they are not convex). Sides are
             invisible twins carrying the contents and surfaceparms, unless extras "visible" is true: then the side
             shader is the face material, its texture matrix fitted from the face UVs.
- patch      patchDef2 from extras "cp" (glTF space + st), "width", "height", "shader".
- entity     extras are the entity keys; origin from the node position. Collision/patch descendants become the
             entity's brushes; render descendants stay static in the world (warned).
Materials resolve to shaders: a shader in --source-base scripts or an image under --source-base or the installed
base keeps its name (its script block and images are packed); an embedded glTF image becomes
textures/levelport/<map>/<material>; anything else gets a grey stand-in. Material extras:
- lp_blend multiply|add|alpha: a non-solid decal (filter, additive, or alpha-blended and vertex-lit).
- lp_emissive {color, intensity, tint}: unlit, glowing, and a q3map2 surface light.
- lp_layers [{texture, tile, weight}]: a terrain blend (Quake 3 terrain style). Each layer is a glTF texture index
  with its own tcMod scale; weight None is the base, 'r'/'g' name the COLOR_0 channel that blends it in. Vertex
  alpha carries one weight, so each triangle gets one two-layer shader: base + 'r' layer where the 'g' weight is
  zero, otherwise the dominant of those two + the 'g' layer.
"""
import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import time
import zipfile
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
import gltfio  # noqa: E402
import levelport as lp  # noqa: E402
import mapio  # noqa: E402
import presets  # noqa: E402
import slabwriter  # noqa: E402
import colcook  # noqa: E402
import colpack  # noqa: E402
import colzbsp  # noqa: E402
import paths  # noqa: E402

ROOT = paths.REPO
GAME = paths.GAMEDATA
# q3map2, read at its first use (an import, --help or --no-compile needs none): $Q3MAP2, the build with
# tools/levelport/q3map2's patches (-bsp about linear in the brush count: map-22 1,250 s -> 23 s, same output; a .map
# past 2 GB, only the planes used: docs/levelport-collision.md). --exact-planes and --cook need exact_planes.patch (a
# chunk compiled alone gets the planes the whole map gives it, docs/levelport-collision.md section 14), --cook also
# brush_bin.patch: $Q3MAP2 when set, else $Q3MAP2_CHUNKS (q3map2/README.md)
Q3MAP2 = None
EXACT_BSP_ARGS = ['-exactplanes', '-pointgrid', '16384']
BSP_EXTRA = []
IMAGE_EXT = ('.tga', '.jpg', '.png')
# Generated shaders for mesh materials: let -meta merge non-coplanar triangles into one lightmapped surface (else
# every facet of a detailed model stays its own draw surface) and smooth lighting across them.
MODEL_LIGHTING = '\tq3map_nonplanar\n\tq3map_shadeAngle 120\n'
SYSTEM_REMAP = {'textures/common/caulk': 'textures/system/caulk', 'textures/common/nodraw': 'textures/system/nodraw',
                'textures/common/clip': 'textures/system/clip', 'textures/common/trigger': 'textures/system/trigger',
                'textures/common/weapclip': 'textures/system/physics_clip', 'textures/common/hint': 'textures/system/hint',
                'textures/common/nodrop': 'textures/system/nodrop', 'textures/common/origin': 'textures/system/origin',
                'textures/common/areaportal': 'textures/system/areaportal'}


# ---------------------------------------------------------------- shader sources

def shader_blocks(text):
    """Shader script text -> {lowercase name: block text}."""
    text = re.sub(r'//[^\n]*', '', text)
    out, i, n = {}, 0, len(text)
    for m in re.finditer(r'(\S+)\s*\{', text):
        if m.start() < i:
            continue
        depth, j = 0, m.end() - 1
        while j < n:
            if text[j] == '{':
                depth += 1
            elif text[j] == '}':
                depth -= 1
                if depth == 0:
                    break
            j += 1
        out.setdefault(m.group(1).lower(), m.group(1) + '\n' + text[m.end() - 1:j + 1])
        i = j + 1
    return out


class Assets:
    """Where a shader name comes from: a source game's scripts and images, or the installed JKA base."""

    def __init__(self, source_base):
        self.src = Path(source_base) if source_base else None
        self.blocks = {}
        if self.src:
            for f in sorted((self.src / 'scripts').glob('*.shader')):
                for k, v in shader_blocks(f.read_text(encoding='latin-1', errors='replace')).items():
                    self.blocks.setdefault(k, v)
        self.jka = set()
        for pk in sorted((GAME / 'base').glob('assets*.pk3')):
            with zipfile.ZipFile(pk) as z:
                for nm in z.namelist():
                    low = nm.lower()
                    if low.endswith(IMAGE_EXT):
                        self.jka.add(low.rsplit('.', 1)[0])
                    elif low.endswith('.shader'):
                        for k in shader_blocks(z.read(nm).decode('latin-1', 'replace')):
                            self.jka.add(k)

    def src_image(self, rel):
        if not self.src:
            return None
        stem = rel.rsplit('.', 1)[0] if rel.lower().endswith(IMAGE_EXT) else rel
        for ext in IMAGE_EXT:
            p = self.src / (stem + ext)
            if p.exists():
                return p
        return None

    def known(self, name):
        low = name.lower()
        return low in self.blocks or self.src_image(name) is not None or low in self.jka


# ---------------------------------------------------------------- compile

class Compiler:
    def __init__(self, doc, name, out, preset, assets, chunk):
        self.doc, self.name, self.out, self.preset, self.assets, self.chunk = doc, name, out, preset, assets, chunk
        meta = doc.doc.get('asset', {}).get('extras', {}).get('levelport', {})
        self.upm = float(meta.get('units_per_metre', lp.UNITS_PER_METRE))
        self.worldspawn = dict(meta.get('worldspawn', {}))
        self.base = out / 'stage' / 'base'
        self.used_shaders = set()      # shaders that need packing (render, visible sides, patches)
        self.twins = {}                # parm tuple -> twin shader name
        self.generated = {}            # generated shader name -> block text
        self.material_shader = {}
        self.stats = {'render_tris': 0, 'chunks': 0, 'brushes': 0, 'hull_brushes': 0, 'bad_brushes': 0, 'patches': 0,
                      'entities': 0, 'dropped_entities': 0, 'static_render_in_entity': 0}
        self.warnings = []
        self.lo, self.hi = np.full(3, np.inf), np.full(3, -np.inf)
        self.sky, self.sky_margin, self.lightmapscale, self.vertexlit = None, 256.0, None, False
        self.chunk_shell = False     # --col-chunk: a temporary structural shell for q3map2's tree, dropped after
        self.worldspawn_extra = {}
        self.merge_verts = 12      # mesh collision: corners of a merged coplanar slab (--merge-verts; 3 = per triangle)
        self.pool = None           # a process pool for the mesh-collision nodes (--jobs)
        self.map_path = None       # --scratch: the .map there, not in the stage
        self.slab_cache = None     # --slab-cache: mesh-collision nodes' brush text kept by their key
        self.map_pieces = None     # run(): the .map's text as written, for an in-memory split
        self.cook = False          # --cook: the brushes as colcook.py records, no .map text
        self.cook_recs = None      # run() with cook: the world's records, in the .map's order
        self.cook_ws = None        # run() with cook: the worldspawn's keys
        self.package = False       # --package: the mesh slabs' inputs kept with their records (colpack.py)
        self.cook_parts = None     # run() with package: the world's records in pieces, as colpack.write takes them

    def extent(self, pts):
        if len(pts):
            self.lo = np.minimum(self.lo, pts.min(0))
            self.hi = np.maximum(self.hi, pts.max(0))

    def sky_shader(self):
        """The sky shell's shader: a stock JKA sky with a baked sun, sky fill and rd-vulkan's live sun."""
        name = self.short('sky')
        if name not in self.generated:
            s = self.sky
            rgb = '%g %g %g' % tuple(s['rgb'])
            self.generated[name] = (
                '%s\n{\n\tqer_editorimage textures/skies/%s_ft\n\tsurfaceparm sky\n\tsurfaceparm noimpact\n'
                '\tsurfaceparm nomarks\n\tnotc\n\tq3map_nolightmap\n\tq3map_lightImage textures/skies/%s_up\n'
                '\tq3map_sunExt %s %g %g %g 2 16\n\tq3map_skylight %g 2\n\tskyParms textures/skies/%s 512 -\n'
                '\tq3gl2_sun %s %g %g %g 0.75 0.5\n}\n' % (
                    name, s['sky'], s['sky'], rgb, s['sun'], s['yaw'], s['elev'], s['skylight'], s['sky'],
                    rgb, s['sun'] / 2, s['yaw'], s['elev']))
        return name

    # ---- materials

    def short(self, leaf):
        """A generated shader path that stays under MAX_QPATH (64) with a PBR sidecar suffix and an image extension
        added ("_orm.jpg"): q3map2 caps shader names at 63 characters and the renderer refuses image paths of 64 or
        more (long leaves keep a hash)."""
        prefix = 'textures/levelport/%s/' % self.name[:24]
        room = 55 - len(prefix)
        if len(leaf) > room:
            leaf = leaf[:room - 9] + '_' + hashlib.sha1(leaf.encode()).hexdigest()[:8]
        return prefix + leaf

    def mat_shader(self, mat_name, mat_idx=None):
        """glTF material -> shader name (full path, textures/...)."""
        key = mat_name or '<none>'
        if key in self.material_shader:
            return self.material_shader[key]
        name = SYSTEM_REMAP.get((mat_name or '').lower(), mat_name)
        if name and self.assets.known(name):
            shader = name
        else:
            safe = re.sub(r'[^a-z0-9_]+', '_', (mat_name or 'none').lower()).strip('_') or 'm'
            shader = self.short(safe)
            img = self._embedded_image(mat_idx)
            water = (self.doc.materials[mat_idx].get('extras') or {}).get('lp_water') if mat_idx is not None else None
            if water:
                self.generated[shader] = self.water_shader(shader, water)
            elif img:
                data, ext = img
                p = self.base / (shader + ext)
                p.parent.mkdir(parents=True, exist_ok=True)
                p.write_bytes(data)
                mat = self.doc.materials[mat_idx]
                self.sidecars(shader, mat.get('normalTexture', {}).get('index'),
                              mat.get('occlusionTexture', {}).get('index'))
                ex = mat.get('extras') or {}
                blend = ex.get('lp_blend')
                if ex.get('lp_emissive'):
                    em = ex['lp_emissive']
                    rgb = [min(1.0, max(0.0, c)) for c in em.get('color', (1, 1, 1))]
                    gen = 'rgbGen const ( %g %g %g )' % tuple(rgb) if em.get('tint') else 'rgbGen identity'
                    self.generated[shader] = (
                        '%s\n{\n\tqer_editorimage %s%s\n\tsurfaceparm nolightmap\n%s\t{\n\t\tmap %s%s\n'
                        '\t\t%s\n\t\tglow\n\t}\n}\n' % (shader, shader, ext, self.surface_light(em), shader, ext, gen))
                elif blend == 'alpha':
                    self.generated[shader] = (
                        '%s\n{\n\tqer_editorimage %s%s\n\tsurfaceparm nonsolid\n\tsurfaceparm trans\n'
                        '\tsurfaceparm nomarks\n\tsurfaceparm nolightmap\n\tpolygonOffset\n\tcull none\n'
                        '\t{\n\t\tmap %s%s\n\t\tblendFunc blend\n\t\trgbGen exactVertex\n\t}\n}\n' % (
                            shader, shader, ext, shader, ext))
                elif blend in ('multiply', 'add'):
                    scroll = ex.get('lp_scroll')
                    tcmod = '\t\ttcMod scroll %g %g\n' % tuple(scroll) if scroll and any(scroll) else ''
                    k = ex.get('lp_add_scale') if blend == 'add' else None
                    gen = 'rgbGen const ( %g %g %g )' % (k, k, k) if k else 'rgbGen identity'
                    self.generated[shader] = (
                        '%s\n{\n\tqer_editorimage %s%s\n\tsurfaceparm nonsolid\n\tsurfaceparm trans\n'
                        '\tsurfaceparm nomarks\n\tsurfaceparm nolightmap\n\tpolygonOffset\n\tcull none\n'
                        '\t{\n\t\tmap %s%s\n%s\t\tblendFunc %s\n\t\t%s\n\t}\n}\n' % (
                            shader, shader, ext, shader, ext, tcmod, 'filter' if blend == 'multiply' else 'add', gen))
                elif mat.get('alphaMode') in ('MASK', 'BLEND'):
                    cull = '\tcull none\n' if mat.get('doubleSided') else ''
                    self.generated[shader] = (
                        '%s\n{\n\tqer_editorimage %s%s\n\tsurfaceparm alphashadow\n\tsurfaceparm trans\n%s%s'
                        '\t{\n\t\tmap %s%s\n\t\talphaFunc GE128\n\t\tdepthWrite\n\t\trgbGen identity\n\t}\n'
                        '\t{\n\t\tmap $lightmap\n\t\tblendFunc GL_DST_COLOR GL_ZERO\n\t\tdepthFunc equal\n'
                        '\t\trgbGen identity\n\t}\n}\n' % (shader, shader, ext, cull, MODEL_LIGHTING, shader, ext))
                else:
                    self.generated[shader] = (
                        '%s\n{\n\tqer_editorimage %s%s\n%s\t{\n\t\tmap $lightmap\n\t\trgbGen identity\n\t}\n'
                        '\t{\n\t\tmap %s%s\n\t\tblendFunc GL_DST_COLOR GL_ZERO\n\t\trgbGen identity\n\t}\n}\n' % (
                            shader, shader, ext, MODEL_LIGHTING, shader, ext))
            elif mat_idx is not None and (self.doc.materials[mat_idx].get('extras') or {}).get('lp_emissive'):
                em = self.doc.materials[mat_idx]['extras']['lp_emissive']
                rgb = [min(1.0, max(0.0, c)) for c in em.get('color', (1, 1, 1))]
                self.generated[shader] = (
                    '%s\n{\n\tqer_editorimage textures/system/nodraw\n\tsurfaceparm nolightmap\n%s'
                    '\t{\n\t\tmap $whiteimage\n\t\trgbGen const ( %g %g %g )\n\t\tglow\n\t}\n}\n' % (
                        shader, self.surface_light(em), *rgb))
            else:
                self.generated[shader] = '%s\n{\n\tqer_editorimage textures/system/nodraw\n%s\t{\n\t\tmap $whiteimage\n' \
                                         '\t\trgbGen const ( 0.5 0.5 0.5 )\n\t}\n\t{\n\t\tmap $lightmap\n' \
                                         '\t\tblendFunc GL_DST_COLOR GL_ZERO\n\t}\n}\n' % (shader, MODEL_LIGHTING)
                self.warnings.append('material %r has no shader or image: grey stand-in %s' % (mat_name, shader))
        self.material_shader[key] = shader
        return shader

    def _embedded_image(self, mat_idx):
        if mat_idx is None:
            return None
        mat = self.doc.materials[mat_idx]
        tex = mat.get('pbrMetallicRoughness', {}).get('baseColorTexture')
        if not tex:
            return None
        img = self.doc.doc['images'][self.doc.doc['textures'][tex['index']]['source']]
        if 'bufferView' in img:
            v = self.doc.doc['bufferViews'][img['bufferView']]
            buf = self.doc.buffers[v.get('buffer', 0)]
            data = buf[v.get('byteOffset', 0):v.get('byteOffset', 0) + v['byteLength']]
        else:
            data = (Path(self.doc_path).parent / img['uri']).read_bytes()
        return data, ('.png' if data[:4] == b'\x89PNG' else '.jpg')

    def mat_extras(self, mat_name):
        for m in self.doc.materials:
            if m.get('name') == mat_name:
                return m.get('extras', {})
        return {}

    def twin(self, contents, side_parms, material=None):
        """material: a JKA surface material (codemp/game/surfaceflags.h names: stone, solidwood, glass, ...) for
        footsteps and impacts, the shader's `material` keyword (node extras "jka_material")."""
        parms = set(p for p in contents if p in lp.CONTENT_PARMS) | set(p for p in side_parms if p in lp.SURFACE_PARMS)
        if 'playerclip' in parms or 'monsterclip' in parms or 'botclip' in parms or 'trigger' in parms:
            parms.discard('nonsolid')
        key = tuple(sorted(parms)) + ((('material', material),) if material else ())
        if key not in self.twins:
            name = self.short('c%d' % len(self.twins))
            body = ['\t// collision twin: %s' % (' '.join(str(k) for k in key) or 'solid'),
                    '\tqer_editorimage textures/system/clip', '\tqer_trans 0.4', '\tsurfaceparm nodraw',
                    '\tsurfaceparm nolightmap', '\tsurfaceparm nomarks']
            body += ['\tsurfaceparm %s' % p for p in sorted(parms) if p != 'nomarks']
            if material:                # q3map2 (ja) sets the bits from q3map_material, not material (w3_castle-2)
                body.append('\tq3map_material %s' % material)
            self.generated[name] = '%s\n{\n%s\n}\n' % (name, '\n'.join(body))
            self.twins[key] = name
        return self.twins[key]

    # ---- geometry

    def q3(self, g):
        return lp.gltf_to_q3(g, self.upm)

    def world_tris(self, mesh_idx, m):
        """[(q3 positions, q3 normals or None, uv or None, triangles (Quake winding), material name, material idx)]"""
        out = []
        prims = self.doc.doc['meshes'][mesh_idx]['primitives']
        for (pos, nrm, uv, tris, mat), raw in zip(self.doc.primitives(mesh_idx), prims):
            wp = pos @ m[:3, :3].T + m[:3, 3]
            wn = None
            if nrm is not None:
                wn = nrm @ np.linalg.inv(m[:3, :3])
                wn /= np.maximum(np.linalg.norm(wn, axis=1, keepdims=True), 1e-9)
                wn = lp.dir_gltf_to_q3(wn)
            t = tris[:, [0, 2, 1]] if np.linalg.det(m[:3, :3]) > 0 else tris
            out.append((self.q3(wp), wn, uv, t, mat, raw.get('material')))
        return out

    def brush_text(self, parts, extras):
        """Convex brush from a collision node's faces."""
        pos = np.concatenate([p[0] for p in parts])
        visible = bool(extras.get('visible'))
        contents = extras.get('contents') or ['solid']
        planes, faces = [], []          # faces: (plane index, material, mat idx, xyz Nx3, uv Nx2)
        for wp, _, uv, tris, mat, midx in parts:
            for t in tris:
                a, b, c = wp[t]
                n = mapio.cross3(b - a, c - a)   # Quake winding is clockwise from the front: (b-a)x(c-a) points in
                ln = np.linalg.norm(n)
                if ln < 1e-6:
                    continue
                n = -n / ln
                d = float(n @ a)
                k = next((i for i, (q, e) in enumerate(planes) if q @ n > 1 - 1e-4 and abs(e - d) < 0.05), None)
                if k is None:
                    planes.append((n, d))
                    k = len(planes) - 1
                faces.append((k, mat, midx, wp[t], uv[t] if uv is not None else None))
        convex = len(planes) >= 4 and all(np.all(pos @ n - d <= 0.05) for n, d in planes)
        if not convex:
            from scipy.spatial import ConvexHull
            try:
                h = ConvexHull(pos)
            except Exception:
                self.stats['bad_brushes'] += 1
                return None
            planes = []
            for eq in h.equations:
                n, d = eq[:3], -eq[3]
                if not any(q @ n > 1 - 1e-4 and abs(e - d) < 0.05 for q, e in planes):
                    planes.append((n, d))
            self.stats['hull_brushes'] += 1
        if self.pool is not None:
            # the faces' polygons (each plane clipped by every other: most of a conversion's time on box and hull
            # shapes) on the pool; run()'s resolve finishes the brush in queue order, so the output is unchanged
            return _PendingBrush(self.pool.submit(_brush_polys, planes), (planes, faces, convex, visible, contents,
                                                                          extras))
        return self.brush_finish(planes, _brush_polys(planes), faces, convex, visible, contents, extras)

    def brush_finish(self, planes, polys, faces, convex, visible, contents, extras):
        """brush_text's second half: the sides from the planes' polygons, their shaders, the brush (text or records)."""
        sides = []
        for i, ((n, d), poly) in enumerate(zip(planes, polys)):
            if poly is None:
                continue
            on = [f for f in faces if (f[0] == i if convex else planes_match(planes[i], f[3]))]
            mat, midx = (on[0][1], on[0][2]) if on else (None, None)
            if visible and mat is not None:
                shader = self.mat_shader(mat, midx)
                self.used_shaders.add(shader)
                xyz = np.concatenate([f[3] for f in on if f[1] == mat])
                st = [f[4] for f in on if f[1] == mat and f[4] is not None]
                tm = mapio.tex_matrix(n, xyz, np.concatenate(st)) if st else None
            else:
                parms = self.mat_extras(mat).get('surfaceparms', []) if mat else []
                shader, tm = self.twin(contents, parms, extras.get('jka_material')), None
            sides.append((poly, n, shader[len('textures/'):], tm))
        if len(sides) < 4:
            self.stats['bad_brushes'] += 1
            return None
        self.stats['brushes'] += 1
        if self.cook:
            # mapio.brush's three points a side, as records (colcook.py)
            names = sorted({sh for _, _, sh, _ in sides})
            pts = [[float(c) for q in mapio.plane_points(poly, n) for c in q] for poly, n, _, _ in sides]
            return colcook.Recs.build([([names.index(sh) for _, _, sh, _ in sides], pts)], names)
        return mapio.brush(sides)

    def mesh_brushes(self, parts, extras):
        """Triangle-mesh collision (extras "shape": "mesh"): one thin brush per triangle. Up-facing triangles are
        extruded down and down-facing ones up, with vertical sides (neighbours share exact planes, as q3map2 terrain
        does) and a cap parallel to the triangle, `thickness` below (above) it measured vertically: a slab. Steep ones
        go along their normal. All are detail: they never split the vis BSP. (A flat cap at the far vertex made every
        sloped face a wedge down to its lowest corner: a stair flight's underside filled the steps above it, sloped
        roofs were solid blocks.)"""
        thick = float(extras.get('thickness', 8.0))
        contents = list(extras.get('contents') or ['solid'])
        if 'detail' not in contents:
            contents.append('detail')
        shader = self.twin(contents, extras.get('surfaceparms', []), extras.get('jka_material'))[len('textures/'):]
        job = ([(wp, tris) for wp, _, _, tris, _, _ in parts], thick, shader, self.merge_verts)
        if self.cook:
            job = job + (self.package,)              # records, not text: the slab cache (text) is not used
            if self.pool is not None:
                return [self.pool.submit(_slab_job_cook, job)]
            recs, st = _slab_job_cook(job)
            for k, v in st.items():
                self.stats[k] = self.stats.get(k, 0) + v
            return [recs]
        keep = None
        if self.slab_cache is not None:
            keep = slab_path(self.slab_cache, job)
            got = slab_get(keep)
            if got is not None:
                texts, st = got
                for k, v in st.items():
                    self.stats[k] = self.stats.get(k, 0) + v
                self.stats['slab_nodes_kept'] = self.stats.get('slab_nodes_kept', 0) + 1
                return texts
            job = job + (keep,)
        if self.pool is not None:      # nodes are independent: run() collects the futures in order
            return [self.pool.submit(_slab_job, job)]
        texts, st = _slab_job(job)
        for k, v in st.items():
            self.stats[k] = self.stats.get(k, 0) + v
        return texts

    def skybox(self, lo, hi, margin, sky):
        """A hollow box of sky brushes around [lo, hi]: seals an outdoor level for vis and lets the sun in."""
        lo, hi = np.asarray(lo) - margin, np.asarray(hi) + margin
        t = 16.0
        out = []
        for ax in range(3):
            for side in (0, 1):
                bl, bh = lo - t, hi + t
                if side == 0:
                    bh = bh.copy()
                    bh[ax] = lo[ax]
                else:
                    bl = bl.copy()
                    bl[ax] = hi[ax]
                planes = []
                for k in range(3):
                    for s in (-1, 1):
                        nn = np.zeros(3)
                        nn[k] = s
                        planes.append((nn, bh[k] if s > 0 else -bl[k]))
                polys = _brush_polys(planes)
                out.append(mapio.brush([(p, n, sky[len('textures/'):], None) for p, (n, _) in zip(polys, planes)]))
        return out

    def patch_text(self, extras, m):
        cp = np.asarray(extras['cp'], dtype=np.float64).reshape(-1, 5)
        g = cp[:, :3] @ m[:3, :3].T + m[:3, 3]
        q = np.concatenate([self.q3(g), cp[:, 3:5]], 1)
        shader = SYSTEM_REMAP.get(extras['shader'].lower(), extras['shader'])
        if not shader.startswith('textures/'):
            shader = self.alias(shader)
        self.used_shaders.add(shader)
        self.stats['patches'] += 1
        return mapio.patch(shader[len('textures/'):], int(extras['width']), int(extras['height']), q)

    def alias(self, shader):
        """Brush sides and patches are always under textures/; give an outside shader a textures/ copy."""
        al = self.short('a_' + re.sub(r'[^a-z0-9_]+', '_', shader.lower()))
        block = self.assets.blocks.get(shader.lower())
        if block:
            self.generated[al] = al + block[block.index('\n'):] + '\n'
            self.used_shaders.add(shader)     # pack its images
        else:
            self.generated[al] = '%s\n{\n\t{\n\t\tmap %s\n\t\trgbGen identity\n\t}\n\t{\n\t\tmap $lightmap\n' \
                                 '\t\tblendFunc GL_DST_COLOR GL_ZERO\n\t}\n}\n' % (al, shader)
            self.used_shaders.add(shader)
        return al

    def water_shader(self, shader, w):
        """Water surface (extras lp_water). Render only; the swimmable volume is a separate water-contents collision
        brush. With `color` and `normal`: one vertex-lit stage of a dark, nearly opaque albedo whose scrolling normal
        map (plus a finer detail normal) and low roughness give rd-vulkan's lit path sun glints and reflection-probe
        reflections, and `waves` deform the surface gently. Otherwise (or in other renderers, which skip the
        material keywords): a tint with two crossing scrolls of a ripple texture."""
        tx, ty = w.get('tile') or (1, 1)
        sx, sy = w.get('scroll') or (0.01, 0.01)
        if w.get('color') is not None and w.get('normal') is not None:
            from PIL import Image
            img = self.base / (shader + '.png')
            img.parent.mkdir(parents=True, exist_ok=True)
            Image.new('RGB', (4, 4), tuple(round(255 * min(1.0, max(0.0, c))) for c in w['color'])).save(img)
            data = self.doc.texture_bytes(w['normal'])
            next_ = shader + '_n' + ('.png' if data[:4] == b'\x89PNG' else '.jpg')
            (self.base / next_).write_bytes(data)
            u = lp.UNITS_PER_METRE
            deform = ''.join('\tdeformVertexes wave %g sin 0 %g 0 %g\n' % (wl * u, amp * u, 1.0 / period)
                             for amp, wl, period in (w.get('waves') or ()))
            ratio, strength = w.get('detail') or (2.3, 0.6)
            return ('%s\n{\n\tqer_editorimage %s.png\n\tqer_trans 0.5\n\tsurfaceparm nonsolid\n\tsurfaceparm trans\n'
                    '\tsurfaceparm nolightmap\n\tsurfaceparm nomarks\n\tcull none\n%s'
                    '\t{\n\t\tmap %s.png\n\t\tnormalMap %s\n\t\tdetailNormalMap %s %g %g\n'
                    '\t\tspecMap $whiteimage\n\t\tspecularReflectance 0.02\n\t\troughness %g\n'
                    '\t\ttcMod scale %g %g\n\t\ttcMod scroll %g %g\n\t\tblendFunc blend\n'
                    '\t\trgbGen exactVertex\n\t\talphaGen const %g\n\t}\n}\n' % (
                        shader, shader, deform, shader, next_, next_, ratio, strength, w.get('roughness', 0.1),
                        tx, ty, sx, sy, w.get('opacity', 0.92)))
        tint = [min(1.0, max(0.0, c)) for c in w.get('tint', (0.2, 0.3, 0.3))]
        if w.get('ripple') is not None:
            img, ext = self.layer_image({'texture': w['ripple']})
            ripple = (img, ext)
        else:
            ripple = ('$whiteimage', '')
        reflect = ''
        if self.sky and w.get('reflect'):     # the level's own sky, environment-mapped: the cheap portable mirror
            k = float(w['reflect'])
            reflect = ('\t{\n\t\tmap textures/skies/%s_up\n\t\ttcGen environment\n\t\tblendFunc GL_ONE GL_ONE\n'
                       '\t\trgbGen const ( %g %g %g )\n\t}\n' % (self.sky['sky'], k, k, k))
        return ('%s\n{\n\tqer_editorimage %s%s\n\tqer_trans 0.5\n\tsurfaceparm nonsolid\n\tsurfaceparm trans\n'
                '\tsurfaceparm nolightmap\n\tsurfaceparm nomarks\n\tcull none\n'
                '\t{\n\t\tmap %s%s\n\t\ttcMod scale %g %g\n\t\ttcMod scroll %g %g\n\t\tblendFunc blend\n'
                '\t\trgbGen const ( %g %g %g )\n\t\talphaGen const %g\n\t}\n'
                '\t{\n\t\tmap %s%s\n\t\ttcMod scale %g %g\n\t\ttcMod scroll %g %g\n\t\tblendFunc GL_DST_COLOR GL_ONE\n'
                '\t\trgbGen const ( 0.35 0.35 0.35 )\n\t}\n%s}\n' % (
                    shader, ripple[0], ripple[1], ripple[0], ripple[1], tx, ty, sx, sy, *tint,
                    w.get('opacity', 0.8), ripple[0], ripple[1], tx * 0.7, ty * 0.7, -sy, sx, reflect))

    def surface_light(self, em):
        """q3map2 surface-light lines for an emissive material (g_Intensity 1 -> 150); none when 'light' is false."""
        if not em.get('light', True):
            return ''
        rgb = [min(1.0, max(0.0, c)) for c in em.get('color', (1, 1, 1))]
        return '\tq3map_surfacelight %g\n\tq3map_lightRGB %g %g %g\n' % (150 * float(em.get('intensity', 1)), *rgb)

    def sidecars(self, img, normal, orm):
        """rd-vulkan's name-discovered PBR maps next to an albedo image: <img>_n (OpenGL normal) and <img>_orm.
        Other renderers never look for them."""
        for tex, suffix in ((normal, '_n'), (orm, '_orm')):
            if tex is None:
                continue
            data = self.doc.texture_bytes(tex)
            p = self.base / (img + suffix + ('.png' if data[:4] == b'\x89PNG' else '.jpg'))
            p.parent.mkdir(parents=True, exist_ok=True)
            p.write_bytes(data)
            self.stats['pbr_maps'] = self.stats.get('pbr_maps', 0) + 1

    def layer_image(self, layer):
        """A terrain layer's albedo as a packed image (with its PBR sidecars) -> (path without extension, ext)."""
        tex_idx = layer['texture']
        key = ('tex', tex_idx)
        if key not in self.material_shader:
            data = self.doc.texture_bytes(tex_idx)
            ext = '.png' if data[:4] == b'\x89PNG' else '.jpg'
            img = self.short('t%d' % tex_idx)
            p = self.base / (img + ext)
            p.parent.mkdir(parents=True, exist_ok=True)
            p.write_bytes(data)
            self.sidecars(img, layer.get('normal'), layer.get('orm'))
            self.material_shader[key] = (img, ext)
        return self.material_shader[key]

    def layer_shader(self, mat_name, layers, base, over):
        """Two-layer terrain shader: layer `base` opaque, layer `over` blended in by vertex alpha. Lightmapped: both
        unlit, then the lightmap multiplies. Vertex-lit: each layer stage is lit by the vertex colour itself
        (rgbGen exactVertex; the alpha is still the weight), which makes them lit stages that rd-vulkan gives
        their _n/_orm maps."""
        safe = re.sub(r'[^a-z0-9_]+', '_', (mat_name or 'm').lower()).strip('_')
        shader = self.short('%s_%d%d' % (safe, base, over) if over is not None else '%s_%d' % (safe, base))
        if shader not in self.generated:
            gen = 'exactVertex' if self.vertexlit else 'identity'
            stages = []
            for k, extra in ((base, ''), (over, '\t\tblendFunc blend\n\t\talphaGen vertex\n')):
                if k is None:
                    continue
                img, ext = self.layer_image(layers[k])
                tx, ty = layers[k].get('tile') or (1, 1)
                stages.append('\t{\n\t\tmap %s%s\n\t\ttcMod scale %g %g\n%s\t\trgbGen %s\n\t}\n' % (
                    img, ext, tx, ty, extra, gen))
            img0, ext0 = self.layer_image(layers[base])
            light = '' if self.vertexlit else ('\t{\n\t\tmap $lightmap\n\t\tblendFunc GL_DST_COLOR GL_ZERO\n'
                                               '\t\trgbGen identity\n\t}\n')
            self.generated[shader] = '%s\n{\n\tqer_editorimage %s%s\n%s%s%s}\n' % (
                shader, img0, ext0, MODEL_LIGHTING, ''.join(stages), light)
        self.used_shaders.add(shader)
        return shader

    def layered(self, wp, wn, uv, tris, col, mat_name, layers):
        """Split a terrain-blend primitive into two-layer shader groups -> render entries with vertex alpha."""
        base = next((i for i, l in enumerate(layers) if not l.get('weight')), 0)
        by = {l['weight']: i for i, l in enumerate(layers) if l.get('weight')}
        r = col[:, 0] if 'r' in by else np.zeros(len(col))
        g = col[:, 1] if 'g' in by else np.zeros(len(col))
        plain = g[tris].max(1) < 8 / 255.0
        r_tri = r[tris].mean(1)
        groups = [(plain, base, by.get('r'), r)]
        if 'g' in by:
            groups += [(~plain & (r_tri <= 0.5), base, by['g'], g),
                       (~plain & (r_tri > 0.5), by.get('r', base), by['g'], g)]
        out = []
        for sel, b, o, alpha in groups:
            if np.any(sel):
                out.append((wp, wn, uv, tris[sel], self.layer_shader(mat_name, layers, b, o),
                            alpha if o is not None else None))
                self.stats['layered_tris'] = self.stats.get('layered_tris', 0) + int(sel.sum())
        return out

    def write_chunks(self, render, anchor):
        """render: [(q3 pos, q3 normal, uv, tris, shader, vertex alpha or None)] -> glTF chunks on the chunk grid,
        misc_model entities. q3map2 reads models through assimp, which keeps glTF COLOR_0 (OBJ carries no alpha);
        the alpha survives -meta and -light as the draw vertices' alpha (alphaGen vertex). Every chunk sits at
        `anchor`, a point inside the playable space: q3map2 floods from entity origins, so an origin out in the void
        would leak the map."""
        cells = {}
        for pos, nrm, uv, tris, shader, alpha in render:
            cent = pos[tris].mean(1)
            key = np.floor(cent / self.chunk).astype(np.int64)
            for cell in np.unique(key, axis=0):
                sel = np.all(key == cell, axis=1)
                cells.setdefault(tuple(cell), []).append((pos, nrm, uv, tris[sel], shader, alpha))
        ents = []
        mdir = self.base / 'maps' / self.name
        mdir.mkdir(parents=True, exist_ok=True)
        for ci, (cell, groups) in enumerate(sorted(cells.items())):
            fn = 'c%03d.glb' % ci
            w = gltfio.Writer()
            prims = []
            for pos, nrm, uv, tris, shader, alpha in groups:
                used = np.unique(tris)
                remap = np.full(len(pos), -1, dtype=np.int64)
                remap[used] = np.arange(len(used))
                p = pos[used] - anchor
                prim = {'position': np.stack([p[:, 0], p[:, 2], -p[:, 1]], 1),           # assimp reads glTF Y-up
                        'uv': uv[used] if uv is not None else np.zeros((len(used), 2)),  # and flips v itself
                        'indices': remap[tris][:, [0, 2, 1]], 'material': w.material(shader)}
                if nrm is not None:
                    n = nrm[used]
                    prim['normal'] = np.stack([n[:, 0], n[:, 2], -n[:, 1]], 1)
                if alpha is not None:
                    a = np.round(np.clip(alpha[used], 0, 1) * 255)
                    prim['color'] = np.concatenate([np.full((len(used), 3), 255.0), a[:, None]], 1)
                prims.append(prim)
                self.stats['render_tris'] += len(tris)
            w.node('chunk%d' % ci, mesh=w.mesh('chunk%d' % ci, prims))
            w.save(mdir / fn)
            keys = {'classname': 'misc_model', 'model': 'maps/%s/%s' % (self.name, fn),
                    'origin': ' '.join(mapio._num(c) for c in anchor), 'spawnflags': '0' if self.vertexlit else '4'}
            if self.lightmapscale:
                keys['_lightmapscale'] = '%g' % self.lightmapscale
            ents.append(mapio.entity(keys))
            self.stats['chunks'] += 1
        return ents

    # ---- scene

    def run(self):
        roles = {}
        for i, nd, m, chain in self.doc.walk():
            ex = nd.get('extras', {}) or {}
            role = ex.get('lp')
            if role is None and 'mesh' in nd:
                role = lp.ROLE_RENDER
            owner = next((c for c in reversed(chain) if (self.doc.nodes[c].get('extras') or {}).get('lp') ==
                          lp.ROLE_ENTITY), None)
            if any((self.doc.nodes[c].get('extras') or {}).get('lp') == lp.ROLE_IGNORE for c in chain):
                continue
            roles[i] = (role, m, owner, ex)

        render, world_body, ent_bodies = [], [], {}
        for i, (role, m, owner, ex) in roles.items():
            nd = self.doc.nodes[i]
            if role == lp.ROLE_RENDER and 'mesh' in nd:
                for (wp, wn, uv, tris, mat, midx), col in zip(self.world_tris(nd['mesh'], m), self.doc.colors(nd['mesh'])):
                    layers = (self.doc.materials[midx].get('extras') or {}).get('lp_layers') if midx is not None else None
                    if layers and col is not None:
                        render.extend(self.layered(wp, wn, uv, tris, col, mat, layers))
                    else:
                        shader = self.mat_shader(mat, midx)
                        self.used_shaders.add(shader)
                        render.append((wp, wn, uv, tris, shader, None))
                    self.extent(wp)
                if owner is not None:
                    self.stats['static_render_in_entity'] += 1
            elif role == lp.ROLE_COLLISION and 'mesh' in nd:
                parts = self.world_tris(nd['mesh'], m)
                body = ent_bodies.setdefault(owner, []) if owner is not None else world_body
                if ex.get('shape') == 'mesh':
                    body.extend(self.mesh_brushes(parts, ex))
                else:
                    txt = self.brush_text(parts, ex)
                    if txt:
                        body.append(txt)
                for wp, *_ in parts:
                    self.extent(wp)
            elif role == lp.ROLE_PATCH:
                txt = self.patch_text(ex, m)
                (ent_bodies.setdefault(owner, []) if owner is not None else world_body).append(txt)

        if self.pool is not None:      # mesh-collision futures -> their brushes, in the order they were queued
            def resolve(body):
                flat = []
                for b in body:
                    if isinstance(b, (str, colcook.Recs)):
                        flat.append(b)
                        continue
                    if isinstance(b, _PendingBrush):
                        txt = self.brush_finish(b.args[0], b.future.result(), *b.args[1:])
                        if txt:
                            flat.append(txt)
                        continue
                    texts, st = b.result()
                    if isinstance(texts, colcook.Recs):     # (--cook: one node's records)
                        flat.append(texts)
                    else:
                        flat.extend(texts)
                    for k, v in st.items():
                        self.stats[k] = self.stats.get(k, 0) + v
                return flat
            world_body = resolve(world_body)
            ent_bodies = {k: resolve(v) for k, v in ent_bodies.items()}

        ents, anchor = [], None
        for i, (role, m, owner, ex) in roles.items():
            if role != lp.ROLE_ENTITY:
                continue
            if anchor is None and str(ex.get('classname', '')).startswith(('info_player', 'team_CTF_redspawn')):
                anchor = np.round(self.q3(m[:3, 3]))
            keys = {k: str(v) for k, v in ex.items() if k != 'lp'}
            keys = self.preset.entity(keys)
            if keys is None:
                self.stats['dropped_entities'] += 1
                continue
            origin = self.q3(m[:3, 3])
            if np.any(np.abs(origin) > 1e-3) and 'origin' not in keys:
                keys['origin'] = ' '.join(mapio._num(c) for c in origin)
            ents.append(mapio.entity(keys, ent_bodies.get(i, [])))
            self.stats['entities'] += 1

        ws = self.preset.worldspawn(self.worldspawn)
        ws.update(self.worldspawn_extra)
        ws['classname'] = 'worldspawn'
        if anchor is None:
            anchor = np.zeros(3)
            self.warnings.append('no spawn point: render chunks anchored at 0 0 0, which may leak')
        if self.sky:
            world_body.extend(self.skybox(self.lo, self.hi, self.sky_margin, self.sky_shader()))
        elif self.chunk_shell and world_body:
            # q3map2 sizes its tree from structural brushes and an all-detail map has none ("Backwards tree
            # volume"): a nodraw box around the chunk, named as a sky shell so colmerge drops it
            name = self.short('sky')
            self.generated[name] = '%s\n{\n\tsurfaceparm nodraw\n\tsurfaceparm nolightmap\n\tsurfaceparm sky\n}\n' % name
            shell = self.skybox(self.lo, self.hi, 64.0, name)
            world_body.extend([colcook.recs_from_text(shell)] if self.cook else shell)
        others = self.write_chunks(render, anchor) + ents      # (writes the chunk models, makes maps/)
        mapfile = self.map_path or self.base / 'maps' / (self.name + '.map')
        mapfile.parent.mkdir(parents=True, exist_ok=True)
        if self.cook:
            # no .map: the world's brushes as one set of records; a chunk is its world alone
            if others or ent_bodies or any(isinstance(b, str) for b in world_body):
                sys.exit('--cook: %s has entities, brush entities or patches (a chunk is brushes only)' % self.name)
            self.cook_recs, self.cook_ws = colcook.join(world_body), ws
            if self.package:
                self.cook_parts = list(world_body)
            self.pack_assets()
            return mapfile

        def pieces():
            # piece by piece, the bytes '\n'.join of the entities would give: a district's worldspawn is gigabytes of
            # text, never held twice
            yield mapio.entity(ws)[:-1]          # the worldspawn's keys; its closing brace after the brushes
            for b in world_body:
                yield b + '\n'
            yield '}'
            for e in others:
                yield '\n' + e
            yield '\n'
        with open(mapfile, 'w', encoding='utf-8', newline='\n') as f:
            for s in pieces():
                f.write(s)
        self.map_pieces = pieces              # (main splits from it, then drops it: the brushes' text stays held)
        self.pack_assets()
        return mapfile

    def pack_assets(self):
        blocks, images = [], set()
        for s in sorted(self.used_shaders):
            if s.startswith('textures/levelport/'):
                continue
            low = s.lower()
            block = self.assets.blocks.get(low)
            if block:
                blocks.append(block)
                images.update(re.findall(r'(?i)\b(?:map|clampmap|qer_editorimage|videomap)\s+(\S+)', block))
                for am in re.findall(r'(?i)\banimmap\s+\S+\s+([^\n]+)', block):
                    images.update(am.split())
                for sky in re.findall(r'(?i)\bskyparms\s+(\S+)', block):
                    if sky != '-':
                        images.update('%s_%s' % (sky, side) for side in ('rt', 'lf', 'bk', 'ft', 'up', 'dn'))
            else:
                images.add(s)
        copied = 0
        for rel in sorted(images):
            if rel.startswith('$') or rel.startswith('*'):
                continue
            p = self.assets.src_image(rel)
            if p:
                dst = self.base / p.relative_to(self.assets.src)
                dst.parent.mkdir(parents=True, exist_ok=True)
                if pot_copy(p, dst):
                    self.stats['resampled_npot'] = self.stats.get('resampled_npot', 0) + 1
                copied += 1
            elif rel.rsplit('.', 1)[0].lower() not in self.assets.jka:
                self.warnings.append('image not found: %s' % rel)
        sh = self.base / 'shaders'
        sh.mkdir(parents=True, exist_ok=True)
        (sh / ('levelport_%s.shader' % self.name)).write_text(
            '// generated by tools/levelport/gltf2map.py\n\n' + '\n'.join(blocks) + '\n' +
            '\n'.join(self.generated[k] for k in sorted(self.generated)), encoding='latin-1', newline='\r\n')
        self.stats['packed_shaders'] = len(blocks)
        self.stats['packed_images'] = copied


def haze_sky(base, name, horizon, zenith, size=256):
    """A generated sky box, textures/skies/<name>_{rt,lf,bk,ft,up,dn}.jpg: a lit haze that depends only on the
    elevation (horizon colour at and below it, the zenith colour straight up; sRGB 0-255 each). A city at night
    under a low cloud deck, the light of its streets thrown back."""
    from PIL import Image
    d = base / 'textures' / 'skies'
    d.mkdir(parents=True, exist_ok=True)
    c = (np.arange(size) + 0.5) / size * 2 - 1
    u, v = np.meshgrid(c, -c)                   # v: +1 at the image's top
    h0, z0 = np.array(horizon, float), np.array(zenith, float)

    def shade(elev):
        t = np.clip(elev / np.radians(70), 0, 1) ** 0.6
        return np.uint8(np.round(h0 + (z0 - h0) * t[..., None]))
    side = shade(np.arctan2(v, np.sqrt(1 + u * u)))
    faces = {'rt': side, 'lf': side, 'bk': side, 'ft': side,
             'up': shade(np.arctan2(1, np.sqrt(u * u + v * v))), 'dn': shade(np.full_like(u, -1.0))}
    for k, img in faces.items():
        Image.fromarray(img).save(d / ('%s_%s.jpg' % (name, k)), quality=95)
    return name


def pano_sky(base, name, pano, yaw=0.0, size=2048):
    """A sky box, textures/skies/<name>_{rt,lf,bk,ft,up,dn}.jpg, sampled from an equirectangular panorama (top row
    straight up; seen from inside, so longitude falls left to right). yaw: the Quake yaw, degrees, its centre column
    faces. Faces as tr_sky.c's MakeSkyVec lays them: rt +X, lf -X, bk +Y, ft -Y, up and dn, image right = the face's
    s axis, image top = its t axis."""
    from PIL import Image
    Image.MAX_IMAGE_PIXELS = None
    P = np.asarray(Image.open(pano).convert('RGB'))
    H, W = P.shape[:2]
    d = base / 'textures' / 'skies'
    d.mkdir(parents=True, exist_ok=True)
    c = (np.arange(size) + 0.5) / size * 2 - 1
    u, v = np.meshgrid(c, -c)
    one = np.ones_like(u)
    faces = {'rt': (one, -u, v), 'lf': (-one, u, v), 'bk': (u, one, v), 'ft': (-u, -one, v),
             'up': (-v, -u, one), 'dn': (v, -u, -one)}
    for k, (x, y, z) in faces.items():
        lon = np.degrees(np.arctan2(y, x)) - yaw
        lat = np.arctan2(z, np.hypot(x, y))
        fx = ((0.5 - lon / 360.0) % 1.0) * W - 0.5
        fy = (0.5 - lat / np.pi) * H - 0.5
        x0, y0 = np.floor(fx).astype(np.int64), np.clip(np.floor(fy).astype(np.int64), 0, H - 1)
        tx, ty = (fx - x0)[..., None], np.clip(fy - y0, 0, 1)[..., None]
        x0 %= W
        x1, y1 = (x0 + 1) % W, np.minimum(y0 + 1, H - 1)
        img = ((P[y0, x0] * (1 - tx) + P[y0, x1] * tx) * (1 - ty) + (P[y1, x0] * (1 - tx) + P[y1, x1] * tx) * ty)
        Image.fromarray(np.uint8(np.clip(np.round(img), 0, 255))).save(d / ('%s_%s.jpg' % (name, k)), quality=94)
    return name


def pot_copy(src, dst):
    """Copy an image; resample non-power-of-two sizes up to the next power of two (the JKA renderers refuse them,
    Quake 3 resampled them). Returns True if resampled."""
    from PIL import Image
    with Image.open(src) as im:
        w, h = im.size
        pw, ph = 1 << (w - 1).bit_length(), 1 << (h - 1).bit_length()
        if (pw, ph) == (w, h):
            shutil.copy2(src, dst)
            return False
        out = im.resize((pw, ph), Image.LANCZOS)
        fmt = {'.jpg': 'JPEG', '.png': 'PNG', '.tga': 'TGA'}[dst.suffix.lower()]
        out.save(dst, fmt, **({'quality': 94} if fmt == 'JPEG' else {}))
    return True


_Z = np.array([0.0, 0.0, 1.0])
STRAIGHT_SIN = 1e-4     # a merged polygon's corner this straight (sine of the turn) is dropped: <= 0.01 u off over 100 u
assert STRAIGHT_SIN == colpack.STRAIGHT_SIN
TURN_SIN = 2e-3         # a corner turning less than this (~0.1 deg) blocks the merge (q3map2 "duplicate plane")


def slab_path(folder, job):
    """A mesh-collision node's file in the slab cache: its key is the node's triangles (positions and indices, as
    world_tris gives them), thickness, shader and merge corners, and the code _slab_job runs (stagekey: _slab_job and
    what it reaches in this file, mapio and q3bsp), so the kept text is what _slab_job would write."""
    import hashlib
    parts, thick, shader, merge_verts = job[:4]
    if not hasattr(slab_path, 'code'):
        import stagekey
        slab_path.code = stagekey.code_key(Path(__file__).resolve(), ['_slab_job'], entry=None)
    h = hashlib.sha1(('%s|%r|%s|%d|%s' % (slab_path.code, float(thick), shader, merge_verts, np.__version__)).encode())
    for wp, tris in parts:
        wp, tris = np.ascontiguousarray(wp, np.float64), np.ascontiguousarray(tris, np.int64)
        h.update(repr((wp.shape, tris.shape)).encode())
        h.update(wp.tobytes())
        h.update(tris.tobytes())
    k = h.hexdigest()[:24]
    return Path(folder) / k[:2] / (k + '.slab')


def slab_get(path):
    """A kept node: (brush texts, stats), or None."""
    import zlib
    try:
        raw = zlib.decompress(path.read_bytes()).decode('utf-8')
    except (OSError, zlib.error, UnicodeDecodeError):
        return None
    head, _, body = raw.partition(chr(0))
    st = json.loads(head)
    return (body.split(chr(0)) if body else []), st


def slab_put(path, texts, stats):
    """Keep a node's result: written whole, then renamed (another process's same entry stands)."""
    import zlib
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_suffix('.tmp%d' % os.getpid())
    tmp.write_bytes(zlib.compress((json.dumps(stats) + chr(0) + chr(0).join(texts)).encode('utf-8'), 1))
    try:
        os.replace(tmp, path)
    except OSError:
        tmp.unlink()


def _slab_job(job):
    """One mesh-collision node's brushes (Compiler.mesh_brushes; a process-pool job): (brush texts, stats); kept in
    the slab cache when the job names its file."""
    parts, thick, shader, merge_verts = job[:4]
    keep = job[4] if len(job) > 4 else None
    out, stats = [], {}

    def emit(pts, n, d):
        # slab_planes, q3_duplicate_planes, _brush_polys and mapio.brush in plain floats: the same text (slabwriter.py)
        pts = [tuple(p.tolist()) if hasattr(p, 'tolist') else tuple(p) for p in pts]
        planes = slabwriter.slab_planes(pts, tuple(n.tolist()), d, thick, STRAIGHT_SIN)
        if planes is None or (len(pts) > 3 and slabwriter.duplicate_planes(planes)):
            return False
        polys = slabwriter.brush_polygons(planes)
        sides = [(poly, pn) for poly, (pn, _) in zip(polys, planes) if poly is not None]
        if len(sides) != len(planes):
            return False
        out.append(slabwriter.brush(sides, shader))
        stats['mesh_brushes'] = stats.get('mesh_brushes', 0) + 1
        return True

    for wp, tris in parts:
        # coplanar edge-neighbours as one convex slab: the same solid as their triangles' slabs (same plane, same
        # vertical sides, same cap), in far fewer brushes; q3map2 -bsp time grows faster than the brush count
        # (Kabuki: 47 k brushes 19 s, 94 k 113 s, 378 k 1,674 s; merged 233 k 823 s)
        for poly, n, d, members in merge_coplanar(wp, tris, stats, merge_verts):
            if emit(poly, n, d):
                continue
            for a, b, c in members:              # a merged polygon that does not make a brush: its triangles
                if not emit((a, b, c), n, d):
                    stats['bad_brushes'] = stats.get('bad_brushes', 0) + 1
    if keep is not None:
        slab_put(keep, out, stats)
    return out, stats


def _slab_job_cook(job):
    """_slab_job's brushes as colcook records (--cook): the same slabs, each side's three points the ones
    slabwriter.brush would write (a separate function, so the slab cache's key, _slab_job's code, stays)."""
    parts, thick, shader, merge_verts = job[:4]
    package = len(job) > 4 and job[4]
    brushes, slabs, stats = [], [], {}

    def emit(pts, n, d):
        # colpack.slab_points: the one slab arithmetic (an expansion of a package runs it again on the same inputs)
        pts = [tuple(p.tolist()) if hasattr(p, 'tolist') else tuple(p) for p in pts]
        n = tuple(n.tolist())
        rec = colpack.slab_points(pts, n, d, float(thick))
        if rec is None:
            return False
        brushes.append(rec)
        if package:
            slabs.append((pts, n, d, float(thick)))
        stats['mesh_brushes'] = stats.get('mesh_brushes', 0) + 1
        return True

    for wp, tris in parts:
        for poly, n, d, members in merge_coplanar(wp, tris, stats, merge_verts):
            if emit(poly, n, d):
                continue
            for a, b, c in members:
                if not emit((a, b, c), n, d):
                    stats['bad_brushes'] = stats.get('bad_brushes', 0) + 1
    r = colcook.Recs.uniform(brushes, shader)
    if package:
        r.slabs = slabs
    return r, stats


def text_lines(pieces):
    """Text given in pieces as its lines, newline kept (as a file is read): colmerge.split from memory."""
    rest = ''
    for s in pieces:
        if '\n' not in s:
            rest += s
            continue
        first, *mid, last = (rest + s).split('\n')
        yield first + '\n'
        for ln in mid:
            yield ln + '\n'
        rest = last
    if rest:
        yield rest


def q3_duplicate_planes(planes):
    """Two sides q3map2 would take for one plane (normals within 1e-5 per component, distances within 0.01): it drops
    one ("duplicate plane") and the brush changes shape."""
    N = np.array([p[0] for p in planes])
    D = np.array([p[1] for p in planes])
    close = (np.abs(N[:, None, :] - N[None, :, :]).max(2) < 1e-5) & (np.abs(D[:, None] - D[None, :]) < 0.01)
    np.fill_diagonal(close, False)
    return bool(close.any())


def slab_planes(pts, n, d, thick):
    """Brush planes of the slab under (over) a convex polygon in plane (n, d), Quake winding: the face, a cap parallel
    to it `thick` deep (vertically for |n.z| > 0.35, along the normal for steeper ones), and a side per edge (vertical,
    or along the normal). Collinear corners are dropped. None if degenerate."""
    if abs(n[2]) > 0.35:
        axis = -_Z if n[2] > 0 else _Z          # extrude away from the front, vertically
        cap = (-n, float(-(d - thick * abs(n[2]))))
    else:
        axis, cap = -n, (-n, float(-d + thick))
    pts = [np.asarray(p, np.float64) for p in pts]
    keep = []
    for i in range(len(pts)):                   # drop corners on a straight edge (a merge leaves them)
        p, q, r = pts[i - 1], pts[i], pts[(i + 1) % len(pts)]
        if np.linalg.norm(mapio.cross3(q - p, r - q)) > STRAIGHT_SIN * max(np.linalg.norm(q - p) * np.linalg.norm(r - q), 1e-12):
            keep.append(q)
    if len(keep) < 3:
        return None
    cen = np.mean(keep, axis=0)
    planes = [(n, d), cap]
    for i in range(len(keep)):
        p, q = keep[i], keep[(i + 1) % len(keep)]
        sn = mapio.cross3(q - p, axis)
        sl = np.linalg.norm(sn)
        if sl < 1e-9:
            return None
        sn /= sl
        if sn @ (cen - p) > 0:
            sn = -sn
        planes.append((sn, float(sn @ p)))
    return planes


def merge_coplanar(wp, tris, stats, max_verts=12, plane_tol=0.1, normal_bins=256):
    """Convex polygons from one node's triangles (Quake winding): triangles that share an edge, a plane (within
    `plane_tol` units at every corner) and an orientation join while the union stays convex and has at most
    `max_verts` corners. Yields (corner points, n, d, member triangles); tiny triangles counted and skipped."""
    groups = {}
    for t in tris:
        a, b, c = wp[t]
        n = -mapio.cross3(b - a, c - a)            # Quake winding (clockwise from the front)
        ln = np.linalg.norm(n)
        if ln < 0.5 or min(np.linalg.norm(b - a), np.linalg.norm(c - b), np.linalg.norm(a - c)) < 0.25:
            stats['tiny_tris'] = stats.get('tiny_tris', 0) + 1
            continue
        n = n / ln
        d = float(n @ a)
        key = (int(round(n[0] * normal_bins)), int(round(n[1] * normal_bins)), int(round(n[2] * normal_bins)),
               int(round(d / max(2.0, 4 * plane_tol))))
        groups.setdefault(key, []).append((a, b, c, n, d))
    for tri_list in groups.values():
        n, d = tri_list[0][3], tri_list[0][4]
        if max_verts < 4 or len(tri_list) == 1:
            for a, b, c, tn, td in tri_list:
                yield (a, b, c), tn, td, [(a, b, c)]
            continue
        pos = {}

        def vid(p):
            k = tuple(np.round(p * 64).astype(np.int64))
            if k not in pos:
                pos[k] = (len(pos), p)
            return pos[k][0]
        polys = {}
        members = {}
        for i, (a, b, c, tn, td) in enumerate(tri_list):
            if max(abs(float(n @ p) - d) for p in (a, b, c)) > plane_tol:    # off the group's plane: alone
                yield (a, b, c), tn, td, [(a, b, c)]
                continue
            polys[i] = [vid(a), vid(b), vid(c)]
            members[i] = [(a, b, c)]
        pts = {v: p for v, p in pos.values()}
        u_ax = mapio.cross3(n, _Z if abs(n[2]) < 0.9 else np.array([1.0, 0.0, 0.0]))
        u_ax /= np.linalg.norm(u_ax)
        v_ax = mapio.cross3(n, u_ax)
        p2 = {v: (float(p @ u_ax), float(p @ v_ax)) for v, p in pts.items()}

        def convex(loop):
            # a corner is straight (dropped by slab_planes), or turns by more than ~0.1 deg: a nearly straight one
            # gives two side planes q3map2 calls one ("duplicate plane", the side dropped, the brush another shape)
            s = 0.0
            for i in range(len(loop)):
                (x0, y0), (x1, y1), (x2, y2) = p2[loop[i - 1]], p2[loop[i]], p2[loop[(i + 1) % len(loop)]]
                cr = (x1 - x0) * (y2 - y1) - (y1 - y0) * (x2 - x1)
                ln = np.hypot(x1 - x0, y1 - y0) * np.hypot(x2 - x1, y2 - y1)
                if abs(cr) <= STRAIGHT_SIN * ln:
                    continue
                if abs(cr) < TURN_SIN * ln:
                    return False
                if s == 0.0:
                    s = cr
                elif (cr > 0) != (s > 0):
                    return False
            return s != 0.0

        def corners(loop):
            n_c = 0
            for i in range(len(loop)):
                (x0, y0), (x1, y1), (x2, y2) = p2[loop[i - 1]], p2[loop[i]], p2[loop[(i + 1) % len(loop)]]
                cr = (x1 - x0) * (y2 - y1) - (y1 - y0) * (x2 - x1)
                n_c += abs(cr) > STRAIGHT_SIN * np.hypot(x1 - x0, y1 - y0) * np.hypot(x2 - x1, y2 - y1)
            return n_c
        edge = {}
        for pid, loop in polys.items():
            for i in range(len(loop)):
                edge[(loop[i], loop[(i + 1) % len(loop)])] = pid
        work = list(polys)
        while work:
            pid = work.pop()
            if pid not in polys:
                continue
            loop = polys[pid]
            merged = False
            for i in range(len(loop)):
                u, v = loop[i], loop[(i + 1) % len(loop)]
                qid = edge.get((v, u))
                if qid is None or qid == pid or qid not in polys:
                    continue
                q = polys[qid]
                j = q.index(v)
                if q[(j + 1) % len(q)] != u:
                    continue
                # P from v round to u, then Q from u round to v (the shared edge dropped)
                pr = loop[i + 1:] + loop[:i + 1]
                qr = q[j + 1:] + q[:j + 1]
                new = pr + qr[1:-1]
                if len(set(new)) != len(new) or not convex(new) or corners(new) > max_verts:
                    continue
                for k in range(len(q)):
                    edge.pop((q[k], q[(k + 1) % len(q)]), None)
                for k in range(len(loop)):
                    edge.pop((loop[k], loop[(k + 1) % len(loop)]), None)
                polys[pid] = new
                members[pid] += members.pop(qid)
                del polys[qid]
                for k in range(len(new)):
                    edge[(new[k], new[(k + 1) % len(new)])] = pid
                work.append(pid)
                stats['merged_tris'] = stats.get('merged_tris', 0) + 1
                merged = True
                break
            if merged:
                continue
        for pid, loop in polys.items():
            yield [pts[v] for v in loop], n, d, members[pid]


def planes_match(plane, tri):
    n, d = plane
    return np.all(np.abs(tri @ n - d) < 0.1)


def _brush_polys(planes):
    import q3bsp
    return q3bsp.brush_polygons([(n, d) for n, d in planes])


class _PendingBrush:
    """A box or hull brush whose polygons are on the pool (Compiler.brush_text); run()'s resolve finishes it."""

    def __init__(self, future, args):
        self.future, self.args = future, args


def q3map2_exe(exact=False, cook=False):
    """the q3map2 to run (see Q3MAP2 above), resolved once; exits naming the variable to set"""
    global Q3MAP2
    if Q3MAP2 is None:
        var = 'Q3MAP2' if os.environ.get('Q3MAP2') or not (exact or cook) else 'Q3MAP2_CHUNKS'
        Q3MAP2 = paths.need(var)
        if cook and b'brushbin' not in Q3MAP2.read_bytes():
            sys.exit('--cook: $%s (%s) has no -brushbin: the build with brush_bin.patch (q3map2/README.md)' % (
                var, Q3MAP2))
    return Q3MAP2


def q3map2(stage, mapfile, root, log):
    args = [str(q3map2_exe()), stage[0], '-game', 'ja', '-fs_basepath', str(GAME), '-fs_basepath', str(root),
            *stage[1:], *(BSP_EXTRA if stage[0] == '-bsp' else []), str(mapfile)]
    t0 = time.time()
    with open(log, 'a', encoding='utf-8') as f:
        f.write('\n$ %s\n' % ' '.join(args))
        f.flush()
        r = subprocess.run(args, stdout=f, stderr=subprocess.STDOUT, cwd=str(q3map2_exe().parent))
    print('  %-7s %6.1f s  exit %d' % (stage[0], time.time() - t0, r.returncode))
    if r.returncode:
        sys.exit('q3map2 failed, see %s' % log)


def q3map2_tiles(parts, root, log, par, avail_gb=5.0):
    """a split chunk's q3map2 tiles, up to `par` at once (they are independent; q3map2 holds about 8 KB a
    brush, so a 500 k-brush tile about 4 GB): one start at a time through memgate.Gate, each reserving `avail_gb`
    until it ends (a tile with nothing else of this run's running starts alone). Each tile logs to its own file,
    appended to `log` in tile order at the end (the tile logs stay); any failure exits as q3map2() does."""
    from concurrent.futures import ThreadPoolExecutor
    import memgate
    gate = memgate.Gate(margin_gb=0.0, log=lambda m: print('  ' + m, flush=True))
    logs = [log.with_name('%s.%s.log' % (log.stem, p.stem)) for p in parts]
    errs = [None] * len(parts)

    def one(k):
        logs[k].write_text('', encoding='utf-8')
        label = 'tile %d' % k
        gate.wait_start(label, avail_gb)
        try:
            q3map2(['-bsp', '-meta'], parts[k], root, logs[k])
        except SystemExit as e:
            errs[k] = str(e)
        finally:
            gate.release(label)

    with ThreadPoolExecutor(par) as ex:
        list(ex.map(one, range(len(parts))))
    with open(log, 'a', encoding='utf-8') as f:
        for lg in logs:
            f.write(lg.read_text(encoding='utf-8', errors='replace'))
    bad = [(p, e) for p, e in zip(parts, errs) if e]
    if bad:
        sys.exit('q3map2 failed on %s, see %s' % (bad[0][0].name, logs[parts.index(bad[0][0])]))


def cook_compare(cooked, rep, a, out, seconds):
    """--cook-compare: the cooked BSP's bytes against the packs' (the gate: byte for byte), into cook_compare.json."""
    import hashlib
    import colbsp
    name = 'maps/%s.bsp' % a.name
    ref = None
    for pk in a.cook_compare.split(','):
        with zipfile.ZipFile(pk) as z:
            if name in z.namelist():
                ref = z.read(name)
                break
    same = ref is not None and cooked == ref
    res = {'chunk': a.name, 'identical': same, 'bytes': len(cooked), 'ref_bytes': None if ref is None else len(ref),
           'sha256': hashlib.sha256(cooked).hexdigest(), 'seconds': round(seconds, 1),
           'cook': {k: v for k, v in rep.items() if k != 'read_kept'}}
    if ref is not None and not same:
        x, y = colbsp.Rbsp(bytes(cooked)), colbsp.Rbsp(ref)
        res['lumps_differ'] = [colbsp.LUMP_NAMES[i] for i in range(colbsp.HEADER_LUMPS)
                               if x.data[x.lumps[i][0]:x.lumps[i][0] + x.lumps[i][1]] !=
                               y.data[y.lumps[i][0]:y.lumps[i][0] + y.lumps[i][1]]]
    (out / 'cook_compare.json').write_text(json.dumps(res, indent=1), encoding='utf-8')
    print('cook-compare: %s %s (%d bytes, %.0f s)' % (a.name, 'IDENTICAL' if same else 'DIFFERENT %s' % (
        res.get('lumps_differ', 'no reference')), len(cooked), seconds))
    return None


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('glb')
    ap.add_argument('name')
    ap.add_argument('--out')
    ap.add_argument('--preset', default='none', choices=sorted(presets.PRESETS))
    ap.add_argument('--source-base', help='extracted source game base (shader scripts and images to pack)')
    ap.add_argument('--chunk', type=float, default=1024.0, help='render chunk size in Quake units')
    ap.add_argument('--light', default='fast', choices=('none', 'fast', 'full'))
    ap.add_argument('--vis', default='full', choices=('none', 'fast', 'full'))
    ap.add_argument('--light-from', help='source Quake 3 / Quake Live BSP whose baked lighting replaces the bake')
    ap.add_argument('--light-scale', type=float, default=4.0, help='source map overbright (Quake 3: 4)')
    ap.add_argument('--sky', help='seal the level in a sky box: stock JKA sky name (yavin, bespin, ...)')
    ap.add_argument('--sun', default='1 0.95 0.85 300 35 45', help='sky sun: r g b intensity yaw elevation')
    ap.add_argument('--sky-haze', help='with --sky: generate the sky box instead, a lit haze from the horizon colour to '
                    'the zenith colour ("hr hg hb zr zg zb", sRGB 0-255); --sky names it')
    ap.add_argument('--sky-pano', help='with --sky: generate the sky box from this equirectangular panorama; --sky names it')
    ap.add_argument('--sky-pano-yaw', type=float, default=0.0,
                    help="--sky-pano: the Quake yaw (degrees) the panorama's centre column faces")
    ap.add_argument('--skylight', type=float, default=400.0)
    ap.add_argument('--lightmapscale', type=float, help='misc_model _lightmapscale (larger = coarser, cheaper)')
    ap.add_argument('--worldspawn', action='append', default=[], help='extra worldspawn key=value (e.g. _ambient=30)')
    ap.add_argument('--gamma', type=float, help='q3map2 -light -gamma (1.6 opens up shade the source lit with GI)')
    ap.add_argument('--vertexlit', action='store_true',
                    help='vertex-light the render models: dense meshes then merge into few large surfaces')
    ap.add_argument('--scale', type=float, default=1.0, help='uniform map scale (1.2 = 20%% larger in Quake units)')
    ap.add_argument('--no-compile', action='store_true')
    ap.add_argument('--jobs', type=int, default=max(1, min(16, (os.cpu_count() or 2) - 2)),
                    help='processes for the mesh-collision nodes (1: in this process)')
    ap.add_argument('--merge-verts', type=int, default=12,
                    help='mesh collision: coplanar neighbours merge into convex slabs of up to this many corners '
                         '(3: one brush per triangle)')
    ap.add_argument('--col-chunk', action='store_true',
                    help='a runtime collision chunk (a city-grid tile in its own frame, docs/levelport-collision.md): '
                         'no sky, light or vis; q3map2 compiles it inside a temporary shell, colmerge.py drops the '
                         'shell and writes its own tree')
    ap.add_argument('--grid-chunks', help='an area\'s play export from city-grid chunks (cp2gltf.py chunks: '
                    'manifest.json, one glb a tile in its own frame): this glb is the area\'s base (ring, lid, spawns, '
                    'entities), each chunk is compiled --col-chunk (unless built) and merged at its offset')
    ap.add_argument('--chunk-builds', help='--grid-chunks: where the chunks\' builds are or go (default <out>/chunks)')
    ap.add_argument('--scratch', help='--col-chunk / --grid-chunks: q3map2\'s .map, .bsp and tiles go in this folder under '
                    'fixed names that the next compile using it overwrites (one folder a worker holds one tile of '
                    'staging, not one a tile); the BSP goes into the pack from there, so --out keeps the pack and logs')
    ap.add_argument('--tile-brushes', type=int, default=500000,
                    help='compile a world of more brushes than this as q3map2 tiles merged under one tree (colmerge.py; '
                         'q3map2 holds about 7.6 KB a brush; 0: always one run)')
    ap.add_argument('--tile-parallel', type=int, default=1,
                    help='--col-chunk: run up to this many of a split chunk\'s q3map2 tiles at once (each start waits '
                         'for 5 GB available less what the running tiles reserve, memgate.py; 1: one after another)')
    ap.add_argument('--slim-snap', type=float, default=0.0,
                    help='after q3map2: one plane per orientation used (colbsp.py slim), box bevels also snapped '
                         'outward to a multiple of this (default 0: exact; 1 saves 4%% of the bytes and changes '
                         'grazing box traces, docs/levelport-collision.md)')
    ap.add_argument('--no-slim', dest='slim_snap', action='store_const', const=None,
                    help="keep q3map2's planes as written")
    ap.add_argument('--slab-cache', default=str(paths.LEVELPORT / 'cache' / 'slabs'),
                    help='--col-chunk: mesh-collision nodes\' brush text kept here by a key of their triangles, '
                         'settings and the writer\'s code, so a node that did not change is not converted again '
                         '(none: off)')
    ap.add_argument('--exact-planes', action='store_true',
                    help="q3map2 -exactplanes -pointgrid 16384 ($Q3MAP2 when set, else $Q3MAP2_CHUNKS): a brush's "
                         'planes depend on the brush alone, so chunks compiled alone match the whole compile')
    ap.add_argument('--cook', action='store_true',
                    help='--col-chunk --exact-planes: no .map text, tiles or q3map2 BSP on disk: the brushes go to q3map2 '
                         '-brushbin as records and the chunk BSP from colcook.py into the pack (byte for byte the text '
                         "path's; collision cooker A). The slab cache is not used")
    ap.add_argument('--xbsz', action='store_true',
                    help='--cook: the pack\'s chunk BSP as XBSZ (colzbsp.py, collision option C: each lump zstd, decoded '
                         'by the engine to the same BSP), stored')
    ap.add_argument('--xbsz-level', type=int, default=19, help='--xbsz: the zstd level (19; its own window, no long mode)')
    ap.add_argument('--cook-compare', metavar='PK3S',
                    help='--cook, a check: the cooked BSP against maps/<name>.bsp in these packs (comma-separated) into '
                         '<out>/cook_compare.json; no pack is written')
    ap.add_argument('--package', metavar='FILE',
                    help='--cook: the chunk as a slab package too (colpack.py: the brushes\' inputs, from which '
                         'colexpand.py makes this BSP again byte for byte)')
    ap.add_argument('--package-only', action='store_true', help='--package and stop: no q3map2, no BSP, no pack')
    ap.add_argument('--no-pack', action='store_true',
                    help='--cook: stop after cooking (with --package: <package>.json holds the BSP\'s sha256 and size, '
                         'what the package expands to); no pack')
    ap.add_argument('--cook-python', action='store_true',
                    help='--cook (implies --exact-planes): the brushes from colbrushes.py, q3map2 -brushbin\'s steps in '
                         'Python, byte for byte its tiles; no q3map2 process (the shader scripts read from the game '
                         'folder and the stage, as q3map2 would)')
    ap.add_argument('--cook-gate', action='store_true',
                    help='--cook, a check: the tiles from both q3map2 -brushbin and colbrushes.py, every tile\'s '
                         'shaders, planes, brushes and sides and the two chunk BSPs compared byte for byte into '
                         '<out>/gate/colbrushes_gate.json (the records kept there as records.npz for colbrushes.py '
                         'gate); the Python BSP goes on')
    a = ap.parse_args(argv)
    if a.cook_python and a.cook_gate:
        sys.exit('--cook-python or --cook-gate, not both')
    if a.cook_python or a.cook_gate:
        if not a.cook:
            sys.exit('--cook-python and --cook-gate need --cook')
        a.exact_planes = True
    if (a.package or a.package_only) and not a.cook:
        sys.exit('--package needs --cook')
    if a.package_only and not a.package:
        sys.exit('--package-only needs --package FILE')
    if a.cook and not (a.col_chunk and a.exact_planes):
        sys.exit('--cook needs --col-chunk --exact-planes')
    if a.exact_planes:
        BSP_EXTRA[:] = EXACT_BSP_ARGS
    if (a.exact_planes or a.cook) and not a.cook_python:
        q3map2_exe(a.exact_planes, a.cook)       # (resolved now: the exact and cook builds come from their variables)
    out = Path(a.out or paths.LEVELPORT / 'build' / ('%s-%s' % (a.name, time.strftime('%Y%m%d-%H%M%S')))).resolve()
    if out.exists() and any(out.iterdir()):
        sys.exit('%s exists and is not empty' % out)
    t0 = time.time()
    doc = gltfio.Document(a.glb)
    comp = Compiler(doc, a.name, out, presets.PRESETS[a.preset](), Assets(a.source_base), a.chunk)
    comp.doc_path = a.glb
    comp.upm *= a.scale
    comp.lightmapscale, comp.vertexlit = a.lightmapscale, a.vertexlit
    comp.worldspawn_extra = dict(kv.split('=', 1) for kv in a.worldspawn)
    comp.merge_verts = a.merge_verts
    comp.cook = a.cook
    comp.package = bool(a.package)
    if a.col_chunk and a.scratch:
        # the staging that nothing reads once the pack is written, under names the next compile overwrites, written
        # there directly (the scratch may be on another drive than --out); q3map2 names its outputs from the .map's
        # file name only (the pack's BSP is named below)
        sdir = Path(a.scratch).resolve()
        sdir.mkdir(parents=True, exist_ok=True)
        comp.map_path = sdir / 'chunk.map'
    if a.col_chunk:
        if a.sky:
            sys.exit('--col-chunk is open: no --sky')
        comp.chunk_shell, a.light, a.vis = True, 'none', 'none'
    if a.sky:
        r, g, b, inten, yaw, elev = (float(x) for x in a.sun.split())
        comp.sky = {'sky': a.sky, 'rgb': (r, g, b), 'sun': inten, 'yaw': yaw, 'elev': elev, 'skylight': a.skylight}
        if a.sky_haze:
            hz = [float(x) for x in a.sky_haze.split()]
            haze_sky(comp.base, a.sky, hz[:3], hz[3:6])
        elif a.sky_pano:
            pano_sky(comp.base, a.sky, a.sky_pano, a.sky_pano_yaw)
    if a.jobs > 1:
        from concurrent.futures import ProcessPoolExecutor
        comp.pool = ProcessPoolExecutor(max_workers=a.jobs)
    if a.col_chunk and a.slab_cache != 'none':
        comp.slab_cache = Path(a.slab_cache).resolve()
    try:
        mapfile = comp.run()
    finally:
        if comp.pool is not None:
            comp.pool.shutdown()
            comp.pool = None
    world = comp.stats.get('brushes', 0) + comp.stats.get('mesh_brushes', 0)
    if a.no_compile or not (a.col_chunk and a.tile_brushes and world > a.tile_brushes):
        comp.map_pieces = None              # (only the in-memory split below reads it)
    print('%s: %s  (%.1f s)' % (a.name, ', '.join('%s %d' % kv for kv in comp.stats.items()), time.time() - t0))
    for w in sorted(set(comp.warnings))[:40]:
        print('  warning:', w)
    # what this build was made from: retain.py keeps a kept build's glb
    (out / 'source.txt').write_text('%s\n%s\n' % (Path(a.glb).resolve(), ' '.join(argv or sys.argv[1:])),
                                    encoding='utf-8')
    if a.no_compile:
        return mapfile
    root, log = out / 'stage', out / 'q3map2.log'
    world = comp.stats.get('brushes', 0) + comp.stats.get('mesh_brushes', 0)
    if a.col_chunk:
        if not world:
            print('  chunk: no collision in it, no BSP')
            (out / 'empty_chunk.txt').write_text('no collision\n', encoding='utf-8')
            return None
        import colmerge
        t_compile = time.time() - 2.0      # a BSP older than this is an earlier compile's, left in the scratch
        bsp = mapfile.with_suffix('.bsp')
        if a.cook and a.package:
            t1 = time.time()
            scripts = {p.relative_to(comp.base).as_posix(): p.read_text(encoding='utf-8')
                       for p in sorted((comp.base / 'shaders').glob('*.shader'))}
            blob = colpack.write(comp.cook_parts, comp.cook_ws, scripts, a.tile_brushes, a.slim_snap, a.name)
            comp.cook_parts = None
            pk = Path(a.package)
            pk.parent.mkdir(parents=True, exist_ok=True)
            pk.with_suffix('.tmp').write_bytes(blob)
            pk.with_suffix('.tmp').replace(pk)
            print('  %-7s %6.1f s  %s, %.1f MB' % ('package', time.time() - t1, pk.name, len(blob) / 1e6))
            if a.package_only:
                print('package: %s  total %.0f s' % (pk, time.time() - t0))
                return pk
        if a.cook:
            t1 = time.time()
            by = 'python' if a.cook_python else 'gate' if a.cook_gate else 'q3map2'
            exe = None if by == 'python' else q3map2_exe()
            q = [str(exe or 'q3map2'), '-bsp', '-game', 'ja', '-fs_basepath', str(GAME), '-fs_basepath', str(root),
                 '-meta', *BSP_EXTRA, str(mapfile)]       # q3map2()'s -bsp command, the .map it would read last
            held, comp.cook_recs = [comp.cook_recs], None      # (cook frees the records once they are cooked)
            cooked, rep = colcook.cook(held, comp.cook_ws, q, a.tile_brushes, a.slim_snap, log,
                                       str(exe.parent) if exe else None, brushes=by, gate_dir=out / 'gate')
            (out / 'cook_report.json').write_text(json.dumps(
                {k: v for k, v in rep.items() if k != 'read_kept'}, indent=1, default=str), encoding='utf-8')
            if 'gate' in rep:
                g = rep['gate']
                print('  gate    %s: %s; chunk BSP %s (%s)' % (
                    'IDENTICAL' if g['identical'] else 'DIFFERENT', g['compared'],
                    'identical' if g['bsp']['identical'] else 'different', out / 'gate' / 'colbrushes_gate.json'))
            if rep['world_brushes'] != world:
                sys.exit('--cook: %d world brushes cooked, the converter made %d' % (rep['world_brushes'], world))
            a.slim_snap = None                   # (cook slims as the step below would)
            if a.package:                        # the BSP this package expands to: its hash beside it
                import hashlib
                Path(a.package + '.json').write_text(json.dumps({
                    'bsp_sha256': hashlib.sha256(cooked).hexdigest(), 'bsp_bytes': len(cooked)}), encoding='utf-8')
            if a.cook_compare:
                return cook_compare(cooked, rep, a, out, time.time() - t0)
            if a.no_pack:
                print('cooked: %s, %d bytes, no pack  total %.0f s' % (a.name, len(cooked), time.time() - t0))
                return None
            sec = rep['seconds']
            print('  %-7s %6.1f s  %d tiles of %s brushes, %d kept; %s merge %.1f s, slim %.1f s' % (
                'cook', time.time() - t1, len(rep['tiles']), '/'.join(str(n) for n in rep['tiles']), rep['brushes'],
                ''.join('%s %.1f s, ' % (k, sec[k]) for k in ('q3map2', 'shaders', 'python') if k in sec),
                sec['merge'], sec['slim']))
        elif a.tile_brushes and world > a.tile_brushes:
            # a chunk of more brushes than one q3map2 run should hold: tiles merged as a chunk (each tile's copy of the
            # shell dropped). With --exact-planes a tile compiled alone gives the whole compile's brushes bit for bit
            # (docs/levelport-collision.md section 14)
            tdir = (sdir if a.scratch else out) / 'tiles'
            t1 = time.time()
            # from the text in memory, the lines the .map holds (reading it back was 3% of a compile)
            rep = colmerge.split(mapfile, tdir, a.tile_brushes, lines=lambda: text_lines(comp.map_pieces()))
            comp.map_pieces = None
            print('  %-7s %6.1f s  %d tiles of %s brushes' % ('split', time.time() - t1, len(rep['tiles']),
                                                              '/'.join(str(n) for n in rep['tiles'])))
            parts = [tdir / ('tile_%d.map' % k) for k in range(len(rep['tiles']))]
            if a.tile_parallel > 1 and len(parts) > 1:
                q3map2_tiles(parts, root, log, a.tile_parallel)
            else:
                for p in parts:
                    q3map2(['-bsp', '-meta'], p, root, log)
            stale = [p for p in parts if p.with_suffix('.bsp').stat().st_mtime < t_compile]
            if stale:
                sys.exit('q3map2 wrote no new BSP for %s' % ', '.join(str(p) for p in stale))
            t1 = time.time()
            rep = colmerge.merge(bsp, [p.with_suffix('.bsp') for p in parts], chunk=True)
        else:
            q3map2(['-bsp', '-meta'], mapfile, root, log)
            if bsp.stat().st_mtime < t_compile:
                sys.exit('q3map2 wrote no new BSP for %s' % mapfile)
            t1 = time.time()
            rep = colmerge.merge(bsp, [bsp], chunk=True)
        print('  %-7s %6.1f s  shell dropped, %d brushes, %d leaves, leaf brushes median %g, max %d' % (
            'chunk', time.time() - t1, rep['brushes'], rep['leafs'], rep['leaf_brushes_median'],
            rep['leaf_brushes_max']))
    elif a.grid_chunks:
        import colmerge
        q3map2(['-bsp', '-meta'], mapfile, root, log)
        parts, offsets = [mapfile.with_suffix('.bsp')], [(0, 0, 0)]
        builds = Path(a.chunk_builds) if a.chunk_builds else out / 'chunks'
        man = json.loads((Path(a.grid_chunks) / 'manifest.json').read_text(encoding='utf-8'))
        upm = man.get('units_per_metre', lp.UNITS_PER_METRE) * a.scale
        t1 = time.time()
        for c in man['chunks']:
            t = c['tile']
            pk3 = builds / t / ('zz_chunk_%s.pk3' % t)
            if (builds / t / 'empty_chunk.txt').exists():
                continue                    # a chunk with no collision
            if not pk3.exists():
                if (builds / t).exists() and any((builds / t).iterdir()):
                    sys.exit('chunk %s: %s holds a build with no pack (stopped?): a new --chunk-builds' % (t, builds / t))
                r = subprocess.run([sys.executable, '-u', str(Path(__file__).resolve()), str(Path(a.grid_chunks) / c['glb']),
                                    'chunk_%s' % t, '--out', str(builds / t), '--col-chunk', '--scale', str(a.scale),
                                    '--jobs', str(a.jobs), *(['--exact-planes'] if a.exact_planes else []),
                                    *(['--scratch', a.scratch] if a.scratch else []),
                                    # (every option that changes a chunk's bytes: the chunks match the run's settings)
                                    '--merge-verts', str(a.merge_verts), '--tile-brushes', str(a.tile_brushes),
                                    '--tile-parallel', str(a.tile_parallel), '--slab-cache', a.slab_cache,
                                    *(['--no-slim'] if a.slim_snap is None else ['--slim-snap', repr(a.slim_snap)])],
                                   capture_output=True, text=True)
                if r.returncode:
                    sys.exit('chunk %s failed:\n%s' % (t, r.stdout[-2000:] + r.stderr[-2000:]))
            bsp = builds / t / 'stage' / 'base' / 'maps' / ('chunk_%s.bsp' % t)
            if not bsp.exists():
                bsp = pk3                   # compiled with --scratch: the BSP is only in the pack (colbsp reads it)
            # the chunk's place in the area: glTF metres, whole units at 37.5 a metre (cp2gltf.py chunks)
            off = lp.gltf_to_q3(np.array(c['offset_in_area_gltf_m'], np.float64), upm)
            if np.any(np.abs(off - np.round(off)) > 1e-6):
                sys.exit('chunk %s: offset %s is not whole units' % (t, off))
            parts.append(bsp)
            offsets.append(np.round(off))
        print('  %-7s %6.1f s  %d chunks' % ('chunks', time.time() - t1, len(parts) - 1))
        t1 = time.time()
        rep = colmerge.merge(mapfile.with_suffix('.bsp'), parts, offsets=offsets)
        print('  %-7s %6.1f s  %d brushes, %d leaves, leaf brushes median %g, max %d' % (
            'merge', time.time() - t1, rep['brushes'], rep['leafs'], rep['leaf_brushes_median'],
            rep['leaf_brushes_max']))
    elif a.tile_brushes and world > a.tile_brushes:
        # q3map2 needs about 7.6 KB a brush: tiles of a bounded brush count, merged under one tree (colmerge.py)
        import colmerge
        if a.vis != 'none':
            sys.exit('--vis needs one q3map2 run: %d brushes is over --tile-brushes %d' % (world, a.tile_brushes))
        tdir = out / 'tiles'
        t1 = time.time()
        rep = colmerge.split(mapfile, tdir, a.tile_brushes)
        print('  %-7s %6.1f s  %d tiles of %s brushes' % ('split', time.time() - t1, len(rep['tiles']),
                                                          '/'.join(str(n) for n in rep['tiles'])))
        parts = [tdir / 'base.map'] + [tdir / ('tile_%d.map' % k) for k in range(len(rep['tiles']))]
        for p in parts:
            q3map2(['-bsp', '-meta'], p, root, log)
        t1 = time.time()
        rep = colmerge.merge(mapfile.with_suffix('.bsp'), [p.with_suffix('.bsp') for p in parts])
        shutil.copy2(tdir / 'base.srf', mapfile.with_suffix('.srf'))    # -light reads the draw surfaces' extras
        print('  %-7s %6.1f s  %d leaves, leaf brushes median %g, max %d' % (
            'merge', time.time() - t1, rep['leafs'], rep['leaf_brushes_median'], rep['leaf_brushes_max']))
    else:
        q3map2(['-bsp', '-meta'], mapfile, root, log)
    if a.vis != 'none':
        q3map2(['-vis', '-saveprt'] + (['-fast'] if a.vis == 'fast' else []), mapfile, root, log)
    if a.light != 'none':
        light = ['-light', '-fast', '-filter', '-patchshadows']
        light += ['-bounce', '1', '-samples', '2'] if a.light == 'fast' else ['-bounce', '4', '-samples', '3', '-bouncegrid']
        if a.gamma:
            light += ['-gamma', '%g' % a.gamma]
        q3map2(light, mapfile, root, log)
    if a.light_from:
        import lighttransfer
        t1 = time.time()
        lighttransfer.main([a.light_from, str(mapfile.with_suffix('.bsp')), '--scale', str(a.light_scale),
                            '--source-scale', str(a.scale)])
        print('  %-7s %6.1f s' % ('transfer', time.time() - t1))
    if a.slim_snap is not None:     # last: -light reads the planes q3map2 wrote
        import colbsp
        t1 = time.time()
        bsp = mapfile.with_suffix('.bsp')
        src = colbsp.read(bsp)
        lumps, rep = colbsp.slim(src, a.slim_snap)
        colbsp.write(bsp, src, lumps)
        del src, lumps
        print('  %-7s %6.1f s  planes %d -> %d' % ('slim', time.time() - t1, rep['planes_before'], rep['planes_after']))
    arena = '{\n\tmap\t\t"%s"\n\tlongname\t"%s"\n\ttype\t\t"%s"\n}\n' % (
        a.name, comp.worldspawn.get('message', a.name), comp.preset.arena_types)
    (comp.base / 'scripts').mkdir(exist_ok=True)
    (comp.base / 'scripts' / (a.name + '.arena')).write_text(arena, encoding='utf-8', newline='\r\n')
    pk3 = out / ('zz_%s.pk3' % a.name)
    files = {p: p for p in comp.base.rglob('*') if p.is_file()}
    cook_bsp = comp.base / 'maps' / (a.name + '.bsp') if a.cook else None
    if cook_bsp is not None:
        files[cook_bsp] = None                  # (its bytes: cooked, in memory)
    scratch_bsp = a.col_chunk and a.scratch and mapfile.with_suffix('.bsp')
    if scratch_bsp:
        files[comp.base / 'maps' / (a.name + '.bsp')] = scratch_bsp     # in the order it has without --scratch
    # written under a temporary name and renamed once it reads back: a stopped write leaves no pack under the final
    # name (cp77_collision and --grid-chunks take an existing pack as built); one that does not read back is .bad
    final, pk3 = pk3, pk3.with_name(pk3.name + '.tmp')
    with zipfile.ZipFile(pk3, 'w', zipfile.ZIP_DEFLATED) as z:
        for p in sorted(files):
            rel = p.relative_to(comp.base).as_posix()
            if p == cook_bsp:
                if a.xbsz:     # collision option C: the BSP as XBSZ, stored (its lumps are compressed already)
                    member = colzbsp.encode(cooked, a.xbsz_level)
                    z.writestr(zipfile.ZipInfo(rel, time.localtime()[:6]), member, zipfile.ZIP_STORED)
                else:
                    member = cooked
                    z.writestr(rel, cooked)
            elif not rel.endswith(('.prt', '.srf', '.map', '.bak', '.obj', '.mtl', '.glb')):
                z.write(files[p], rel)
    if cook_bsp is not None:
        with zipfile.ZipFile(pk3) as z:
            bad = z.testzip()
            size = z.getinfo('maps/%s.bsp' % a.name).file_size
        if not bad and a.xbsz:
            with zipfile.ZipFile(pk3) as z:
                if colzbsp.decode(z.read('maps/%s.bsp' % a.name)) != bytes(cooked):
                    bad = 'the XBSZ member does not decode to the BSP'
        if bad or size != len(member):
            os.replace(pk3, final.with_name(final.name + '.bad'))
            sys.exit('%s.bad: the pack does not read back (%s)' % (final, bad or 'BSP size %d, not %d' % (size, len(member))))
        del cooked, member
    if scratch_bsp:
        # read back before the scratch is reused: the next compile overwrites the only other copy
        with zipfile.ZipFile(pk3) as z:
            bad = z.testzip()
            size = z.getinfo('maps/%s.bsp' % a.name).file_size
        if bad or size != scratch_bsp.stat().st_size:
            os.replace(pk3, final.with_name(final.name + '.bad'))
            sys.exit('%s.bad: the pack does not read back (%s)' % (final, bad or 'BSP size %d, not %d' % (
                size, scratch_bsp.stat().st_size)))
    os.replace(pk3, final)
    pk3 = final
    print('pk3: %s (%.1f MB)  total %.0f s' % (pk3, pk3.stat().st_size / 1e6, time.time() - t0))
    return pk3


if __name__ == '__main__':
    import jobguard
    jobguard.kill_children_with_me()      # (its pool and q3map2 end with it)
    main()
