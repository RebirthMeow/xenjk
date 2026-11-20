"""Export a compiled Quake 3 / Quake Live BSP to a levelport glTF (.glb).

    python tools/levelport/bsp2gltf.py <map.bsp> <out.glb> [--base DIR] [--texsize 512]

What goes in (see README.md for the node convention):
- render: every drawn planar and triangle-soup surface of the world, one primitive per shader, UVs and normals.
- collision: one node per gameplay brush (solid, clip, liquid, fog, trigger), its faces carrying their side shaders.
  Brushes of movers (doors, plats, func_static) are marked visible and carry UVs fitted from the drawn faces.
- patch: each curved patch, control points in extras, a tessellated preview mesh.
- entity: every entity except worldspawn (whose keys go in the asset extras), brush models as child nodes.
Materials are named by shader; their extras carry the shader's surfaceparms from the BSP shader lump. With --base
(an extracted baseq3), base colour images are embedded, downscaled to --texsize.
"""
import argparse
import io
import re
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
import gltfio  # noqa: E402
import levelport as lp  # noqa: E402
import mapio  # noqa: E402
import q3bsp  # noqa: E402

MOVERS = ('func_door', 'func_plat', 'func_bobbing', 'func_rotating', 'func_train', 'func_button', 'func_static',
          'func_pendulum', 'func_breakable', 'func_glass', 'func_usable')
SKIP_CLASSES = ('advertisement',)
SKIP_SHADER_PREFIX = ('textures/ad_content/',)


def shader_parms(bsp, i):
    c = int(bsp.shaders[i]['content_flags']) & 0xffffffff
    s = int(bsp.shaders[i]['surface_flags']) & 0xffffffff
    contents = q3bsp.flag_names(c, q3bsp.CONTENTS)
    surf = q3bsp.flag_names(s, q3bsp.SURF)
    parms = [n for n in contents if n in lp.CONTENT_PARMS] + [n for n in surf if n in lp.SURFACE_PARMS]
    if 'nonsolid' in surf:
        parms.append('nonsolid')
    return contents, surf, parms


# ---------------------------------------------------------------- textures

class Textures:
    """Shader name -> encoded image bytes for a glTF preview, from an extracted baseq3 (shader scripts + images)."""

    def __init__(self, base, size):
        self.base, self.size, self.cache = Path(base) if base else None, size, {}
        self.scripts = {}
        if self.base:
            for f in sorted((self.base / 'scripts').glob('*.shader')):
                self._parse(f.read_text(encoding='latin-1', errors='replace'))

    def _parse(self, text):
        text = re.sub(r'//[^\n]*', '', text)
        toks = re.findall(r'[{}]|[^\s{}]+', text)
        i, n = 0, len(toks)
        while i < n:
            name = toks[i].lower()
            if i + 1 < n and toks[i + 1] == '{':
                depth, j, image, editor = 0, i + 1, None, None
                while j < n:
                    t = toks[j]
                    if t == '{':
                        depth += 1
                    elif t == '}':
                        depth -= 1
                        if depth == 0:
                            break
                    elif t.lower() == 'qer_editorimage' and j + 1 < n:
                        editor = toks[j + 1]
                    elif t.lower() in ('map', 'clampmap') and depth == 2 and j + 1 < n and image is None \
                            and not toks[j + 1].startswith('$'):
                        image = toks[j + 1]
                    elif t.lower() == 'animmap' and depth == 2 and j + 2 < n and image is None:
                        image = toks[j + 2]
                    j += 1
                self.scripts.setdefault(name, editor or image)
                i = j + 1
            else:
                i += 1

    def _find(self, rel):
        stem = Path(rel).with_suffix('')
        for ext in ('.tga', '.jpg', '.png'):
            p = self.base / (str(stem) + ext)
            if p.exists():
                return p
        return None

    def get(self, shader):
        if not self.base:
            return None
        if shader in self.cache:
            return self.cache[shader]
        out = None
        for cand in (self.scripts.get(shader.lower()), shader):
            p = self._find(cand) if cand else None
            if p:
                try:
                    from PIL import Image
                    im = Image.open(p)
                    im.thumbnail((self.size, self.size))
                    buf = io.BytesIO()
                    if im.mode in ('RGBA', 'LA', 'P'):
                        im.convert('RGBA').save(buf, 'PNG')
                        out = (buf.getvalue(), 'image/png')
                    else:
                        im.convert('RGB').save(buf, 'JPEG', quality=88)
                        out = (buf.getvalue(), 'image/jpeg')
                except Exception as e:  # unreadable image: no preview, the shader name still carries it
                    print('  texture %s: %s' % (p, e))
                break
        self.cache[shader] = out
        return out


# ---------------------------------------------------------------- geometry

def tessellate_patch(cp, w, h, steps=6):
    """Biquadratic Bezier patch grid (h x w x k) -> (vertices, triangles) at `steps` per 3x3 subpatch."""
    cp = np.asarray(cp, dtype=np.float64).reshape(h, w, -1)
    t = np.linspace(0, 1, steps + 1)
    b = np.stack([(1 - t) ** 2, 2 * t * (1 - t), t ** 2], axis=1)     # steps+1 x 3
    rows = []
    for pi in range(0, h - 1, 2):
        row = []
        for pj in range(0, w - 1, 2):
            sub = cp[pi:pi + 3, pj:pj + 3]                             # 3x3xk
            grid = np.einsum('ia,jb,abk->ijk', b, b, sub)              # (steps+1)^2 x k
            row.append(grid if pj == 0 else grid[:, 1:])
        g = np.concatenate(row, axis=1)
        rows.append(g if pi == 0 else g[1:])
    g = np.concatenate(rows, axis=0)
    gh, gw = g.shape[:2]
    idx = np.arange(gh * gw).reshape(gh, gw)
    a, bq, c, d = idx[:-1, :-1].ravel(), idx[:-1, 1:].ravel(), idx[1:, 1:].ravel(), idx[1:, :-1].ravel()
    tris = np.concatenate([np.stack([a, c, bq], 1), np.stack([a, d, c], 1)])
    return g.reshape(gh * gw, -1), tris


def surface_tris(bsp, s):
    v = bsp.verts[s['first_vert']:s['first_vert'] + s['num_verts']]
    idx = bsp.indexes[s['first_index']:s['first_index'] + s['num_indexes']].reshape(-1, 3)
    return v, idx


def brush_sides(bsp, bi):
    br = bsp.brushes[bi]
    sides = bsp.brushsides[br['first_side']:br['first_side'] + br['num_sides']]
    planes = [(bsp.planes[s['plane']]['normal'].astype(np.float64), float(bsp.planes[s['plane']]['dist']))
              for s in sides]
    polys = q3bsp.brush_polygons(planes)
    return [(poly, n, d, int(s['shader'])) for poly, (n, d), s in zip(polys, planes, sides) if poly is not None]


def fit_side_uv(bsp, model, poly, n, d, shader):
    """UVs for a mover's brush side from the drawn planar faces it produced (same shader, same plane)."""
    xyz, st = [], []
    for si in range(model['first_surface'], model['first_surface'] + model['num_surfaces']):
        s = bsp.surfaces[si]
        if s['shader'] != shader or s['type'] != q3bsp.MST_PLANAR:
            continue
        v = bsp.verts[s['first_vert']:s['first_vert'] + s['num_verts']]
        p = v['xyz'].astype(np.float64)
        if np.all(np.abs(p @ n - d) < 0.25):
            xyz.append(p)
            st.append(v['st'].astype(np.float64))
    if not xyz:
        return None
    tm = mapio.tex_matrix(n, np.concatenate(xyz), np.concatenate(st))
    if tm is None:
        return None
    tx, ty = mapio.axis_base(n)
    uvw = np.stack([poly @ tx, poly @ ty, np.ones(len(poly))], 1)
    return uvw @ tm.T


class Exporter:
    def __init__(self, bsp, textures):
        self.bsp, self.tex, self.w = bsp, textures, gltfio.Writer()
        self.stats = {'render_tris': 0, 'brushes': 0, 'visible_brushes': 0, 'patches': 0, 'entities': 0,
                      'skipped_brushes': 0, 'uv_fit_missing': 0}

    def material(self, shader_idx):
        name = self.bsp.shader_name(shader_idx)
        if name in self.w.material_index:
            return self.w.material_index[name]
        contents, surf, parms = shader_parms(self.bsp, shader_idx)
        img = self.tex.get(name)
        extras = {'shader': name, 'surfaceparms': parms, 'q3_contents': contents, 'q3_surface': surf}
        return self.w.material(name, image=img[0] if img else None, mime=img[1] if img else None, extras=extras,
                               alpha='MASK' if 'trans' in contents else None)

    def render_node(self, name, surf_range, parent):
        by_shader = {}
        for si in surf_range:
            s = self.bsp.surfaces[si]
            if s['type'] not in (q3bsp.MST_PLANAR, q3bsp.MST_TRISOUP) or s['num_indexes'] == 0:
                continue
            sname = self.bsp.shader_name(s['shader'])
            contents, surf, _ = shader_parms(self.bsp, s['shader'])
            if 'nodraw' in surf or 'fog' in contents or sname.startswith(SKIP_SHADER_PREFIX):
                continue
            by_shader.setdefault(int(s['shader']), []).append(s)
        prims = []
        for sh, surfs in sorted(by_shader.items()):
            pos, nrm, uv, col, tris, base = [], [], [], [], [], 0
            for s in surfs:
                v, idx = surface_tris(self.bsp, s)
                pos.append(v['xyz'])
                nrm.append(v['normal'])
                uv.append(v['st'])
                col.append(v['color'][..., 0, :] if v['color'].ndim == 3 else v['color'])
                tris.append(idx + base)
                base += len(v)
            tris = np.concatenate(tris)
            tris = tris[:, [0, 2, 1]]            # Quake front faces are clockwise seen from the front; glTF is CCW
            prims.append({'position': lp.q3_to_gltf(np.concatenate(pos)), 'normal': lp.dir_q3_to_gltf(np.concatenate(nrm)),
                          'uv': np.concatenate(uv), 'color': np.concatenate(col), 'indices': tris,
                          'material': self.material(sh)})
            self.stats['render_tris'] += len(tris)
        if prims:
            self.w.node(name, mesh=self.w.mesh(name, prims), extras={'lp': lp.ROLE_RENDER}, parent=parent)

    def patch_nodes(self, surf_range, parent):
        for si in surf_range:
            s = self.bsp.surfaces[si]
            if s['type'] != q3bsp.MST_PATCH:
                continue
            sname = self.bsp.shader_name(s['shader'])
            v = self.bsp.verts[s['first_vert']:s['first_vert'] + s['num_verts']]
            w, h = int(s['patch_w']), int(s['patch_h'])
            cp = np.concatenate([lp.q3_to_gltf(v['xyz']), v['st']], axis=1)     # glTF space, st
            grid, tris = tessellate_patch(np.concatenate([v['xyz'], v['st']], 1), w, h)
            prim = {'position': lp.q3_to_gltf(grid[:, :3]), 'uv': grid[:, 3:5], 'indices': tris[:, [0, 2, 1]],
                    'material': self.material(s['shader'])}
            extras = {'lp': lp.ROLE_PATCH, 'shader': sname, 'width': w, 'height': h,
                      'cp': np.round(cp, 6).ravel().tolist()}
            self.w.node('patch_%d' % si, mesh=self.w.mesh('patch_%d' % si, [prim]), extras=extras, parent=parent)
            self.stats['patches'] += 1

    def brush_nodes(self, model, parent, visible, bmodel_name):
        b = self.bsp
        for bi in range(model['first_brush'], model['first_brush'] + model['num_brushes']):
            contents, _, _ = shader_parms(b, b.brushes[bi]['shader'])
            if not visible and not any(c in lp.GAMEPLAY_CONTENTS for c in contents):   # movers keep decal brushes
                self.stats['skipped_brushes'] += 1
                continue
            sides = brush_sides(b, bi)
            if len(sides) < 4:
                self.stats['skipped_brushes'] += 1
                continue
            by_mat = {}
            fog = 'fog' in contents
            for poly, n, d, sh in sides:
                uv = None
                if visible or fog:
                    uv = fit_side_uv(b, model, poly, n, d, sh)
                    if uv is None:
                        self.stats['uv_fit_missing'] += 1
                        uv = np.stack([poly @ mapio.axis_base(n)[0], poly @ mapio.axis_base(n)[1]], 1) / 128.0
                by_mat.setdefault(sh, []).append((poly, n, uv))
            prims = []
            for sh, faces in by_mat.items():
                pos, nrm, uvs, tris, base = [], [], [], [], 0
                for poly, n, uv in faces:
                    k = len(poly)
                    pos.append(poly)
                    nrm.append(np.repeat(n[None], k, 0))
                    if uv is not None:
                        uvs.append(uv)
                    # brush polygons wind clockwise seen from outside (Quake), so fan (0, i+1, i) is CCW for glTF
                    tris.append(np.array([[base, base + i + 1, base + i] for i in range(1, k - 1)]))
                    base += k
                prims.append({'position': lp.q3_to_gltf(np.concatenate(pos)),
                              'normal': lp.dir_q3_to_gltf(np.concatenate(nrm)),
                              'uv': np.concatenate(uvs) if uvs else None, 'indices': np.concatenate(tris),
                              'material': self.material(sh)})
            extras = {'lp': lp.ROLE_COLLISION, 'contents': contents}
            if visible or fog:
                extras['visible'] = True
                self.stats['visible_brushes'] += 1
            self.w.node('%s_brush%d' % (bmodel_name, bi), mesh=self.w.mesh('brush%d' % bi, prims), extras=extras,
                        parent=parent)
            self.stats['brushes'] += 1

    def run(self, source):
        b = self.bsp
        world = b.models[0]
        rng = range(world['first_surface'], world['first_surface'] + world['num_surfaces'])
        g_render = self.w.node('render')
        self.render_node('world', rng, g_render)
        g_patch = self.w.node('patches')
        self.patch_nodes(rng, g_patch)
        g_col = self.w.node('collision')
        self.brush_nodes(world, g_col, False, 'world')
        g_ent = self.w.node('entities')
        worldspawn = {}
        for ei, e in enumerate(b.entities):
            cls = e.get('classname', '')
            if cls == 'worldspawn':
                worldspawn = dict(e)
                continue
            if cls in SKIP_CLASSES:
                continue
            keys = {k: v for k, v in e.items() if k != 'origin'}
            origin = np.array([float(c) for c in e.get('origin', '0 0 0').split()[:3]])
            model = e.get('model', '')
            if model.startswith('*'):
                keys.pop('model')
            node = self.w.node('%s_%d' % (cls, ei), translation=lp.q3_to_gltf(origin),
                               extras=dict(keys, lp=lp.ROLE_ENTITY), parent=g_ent)
            self.stats['entities'] += 1
            if model.startswith('*'):
                m = b.models[int(model[1:])]
                srng = range(m['first_surface'], m['first_surface'] + m['num_surfaces'])
                # brush model geometry is stored in world space; child nodes are offset back by the entity origin
                sub = self.w.node('%s_geometry' % cls, translation=-lp.q3_to_gltf(origin), parent=node)
                self.brush_nodes(m, sub, cls in MOVERS, 'm%s' % model[1:])
                self.patch_nodes(srng, sub)
                soup = [si for si in srng if b.surfaces[si]['type'] == q3bsp.MST_TRISOUP]
                if soup:
                    print('  %s %s: %d triangle-soup surfaces stay static' % (cls, model, len(soup)))
                    self.render_node('%s_soup' % cls, soup, sub)
        self.w.save(self.out, extras={'levelport': {
            'version': 1, 'units_per_metre': lp.UNITS_PER_METRE, 'source': str(source), 'source_game':
            'quakelive' if b.version == 47 else 'quake3', 'worldspawn': worldspawn}})


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('bsp')
    ap.add_argument('out')
    ap.add_argument('--base', help='extracted baseq3 for shader scripts and preview textures')
    ap.add_argument('--texsize', type=int, default=512)
    a = ap.parse_args(argv)
    bsp = q3bsp.Bsp(a.bsp)
    ex = Exporter(bsp, Textures(a.base, a.texsize))
    ex.out = a.out
    ex.run(Path(a.bsp).name)
    print('%s -> %s (%.1f MB)' % (a.bsp, a.out, Path(a.out).stat().st_size / 1e6))
    print('  ' + ', '.join('%s %d' % kv for kv in ex.stats.items()))


if __name__ == '__main__':
    main()
