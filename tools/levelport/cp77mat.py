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
"""Cyberpunk 2077 materials (WolvenKit .Material.json) -> flat levelport materials.

Textures come from a WolvenKit uncook folder (`root`): depot paths with .xbm replaced by .png, mltemplates as
<depot>.mltemplate.json, mlmask layers as <stem>_layers/<stem>_<i>.png beside the mask's depot path.

  Materials(root, size).convert(material) -> dict(kind, image (RGBA uint8 array or None), normal, orm, extras)

kinds: 'opaque', 'cutout' (alpha test), 'blend' (alpha decal or glass), 'emissive' (unlit glow, image optional),
'skip' (nothing to draw: shadow proxies, holograms of nothing). Emissives are not q3map2 surface lights
(lp_emissive light false): the sectors' own light nodes light the scene, and Kabuki's signs and windows made
50k area lights.

Multilayered materials are composited once at a fixed resolution (after rend2_comparison/tools/cp_multilayer.py,
the vehicle pipeline): each layer is a tiled .mltemplate (colour/normal/roughness/metal) blended bottom-up by its
mask, with the named colorScale tint and roughness/metal output ranges. Template colour textures are detail maps
centred on 0.5 linear: the tint is the albedo, the texture x2. Microblends only break the mask threshold.

With detail_size, the most visible layer is not baked at its tiling (a 512 bake keeps a few dozen pixels a repeat):
the bake takes its mean colour and roughness and a flat normal, its ORM's red says where it shows, and
detail_image(template) is its tiling texture as zero-mean detail (rg normal slope, b brightness over its mean / 2,
a roughness over its mean + 0.5) that the renderer tiles at the source's own rate (multilayer()'s `detail`).
"""
import hashlib
import json
import os
import re
from collections import OrderedDict
from contextlib import contextmanager
from pathlib import Path

import numpy as np
from PIL import Image

SL = chr(92)
# a signal head's lamps (lights_interactive zones, which lamp lights which zone is the entity's: cp2gltf): the car
# lights' red, yellow, green, a crossing's DON'T WALK hand (its red lamp) and walking figure (its green); display
# colours of the game's signal faces by eye
LAMP_COLOURS = {'red': (1.0, 0.22, 0.06), 'yellow': (1.0, 0.55, 0.08), 'green': (0.1, 1.0, 0.45),
                'walk': (0.75, 1.0, 0.95)}
DEFAULTS = ('engine' + SL + 'textures' + SL + 'editor',)


def _num(d, key, comp=None, default=0.0):
    """a material field as a float: a scalar, or one component (X Y Z W) of a vector field"""
    v = d.get(key)
    if isinstance(v, dict):
        v = v.get(comp or 'X')
    try:
        return float(v) if v is not None else float(default)
    except (TypeError, ValueError):
        return float(default)


def first_ev(d, keys, default):
    """The first of keys a material gives (an explicit 0 included), as a float, else default: `a or b or 5.0` lit an
    EV-0 lamp at EV 5"""
    return float(next((d[k] for k in keys if d.get(k) is not None), default))


def _layers(d):
    """layers 2-4's strengths relative to the first's (IntensityPerLayer X: 3 on L_0_emissive_downtown, whose Y is 3;
    the renderer weighs the first layer 1)"""
    x = _num(d, 'IntensityPerLayer', 'X', default=1.0)
    x = x if x > 0 else 1.0
    return [_num(d, 'IntensityPerLayer', c) / x for c in 'YZW']


def screen_params(t, d, tiles=None):
    """A screen material's 16 floats for maps/<map>.xscr:
    template (1 tv, 2 ui, 3 parallax, 4 parallax transparent, 5 hologram), flipbook tiles w, h and frames per second,
    grid rows, grid darkness (CP2077's raw scanline intensity: 2-20 on some holograms), lines (0) or dots (1), grid
    fade distance (m), layer separation, layers 2-4 strength (relative to the first),
    blink speed, hue cycle speed, broken, additive. tiles: the flipbook the image really holds (w, h), else the
    material's own."""
    p = [0.0] * 16
    if t == 'television_ad.mt':
        p[0] = 1.0
        p[1], p[2] = tiles or (max(_num(d, 'TilesWidth', default=1), 1), max(_num(d, 'TilesHeight', default=1), 1))
        p[3], p[4], p[5] = _num(d, 'PlaySpeed'), _num(d, 'PixelsHeight'), _num(d, 'BlackLinesIntensity')
        p[6], p[7], p[13], p[14] = (_num(d, 'LinesOrDots'), _num(d, 'DistanceDivision'), _num(d, 'HUEChangeSpeed'),
                                    _num(d, 'IsBroken'))
    elif t in ('metal_base_ui.mt', 'parallaxscreen_transparent_ui.mt'):
        p[0] = 2.0
        p[1], p[2] = tiles or (1.0, 1.0)
        p[4], p[5], p[6] = _num(d, 'ScanlinesDensity', 'X'), _num(d, 'ScanlinesIntensity'), _num(d, 'ScanlinesDensity', 'W')
        p[8] = _num(d, 'LayersSeparation')
        p[9], p[10], p[11] = _layers(d)
        p[14] = _num(d, 'IsBroken')
    elif t in ('parallaxscreen.mt', 'parallaxscreen_transparent.mt', 'screen.mt', 'screen_fluff.mt'):
        p[0] = 4.0 if t == 'parallaxscreen_transparent.mt' else 3.0
        # (tile counts as CP2077 writes them, fractions too: a cell is 1/w x 1/h of the sheet - street_a_plate_b's
        # ads_set_2x4 is 0.5 x 4 at 0.2 frames a second; office_security_gate's bars 1.5 x 90 at 0 over a mask)
        p[1], p[2] = tiles or tuple(_num(d, k, default=1) if _num(d, k, default=1) > 0 else 1.0     # (0: unset)
                                    for k in ('TilesWidth', 'TilesHeight'))
        p[3], p[4], p[5] = _num(d, 'PlaySpeed'), _num(d, 'ScanlinesDensity'), _num(d, 'ScanlinesIntensity')
        p[8] = _num(d, 'LayersSeparation')
        p[9], p[10], p[11] = _layers(d)
        p[12], p[13], p[14], p[15] = (_num(d, 'BlinkingSpeed'), _num(d, 'HSV_Mod', 'X'), _num(d, 'IsBroken'),
                                      _num(d, 'AdditiveAlphaBlend'))
    elif t in ('hologram.mt', 'hologram_two_sided.mt'):
        p[0] = 5.0
        p[1], p[2] = tiles or (1.0, 1.0)
        p[4], p[12], p[14], p[15] = (_num(d, 'OpaqueScanlineDensity'), _num(d, 'FlickerChance'), _num(d, 'GlitchChance'),
                                     _num(d, 'AdditiveAlphaBlend'))
    elif t == 'particles_hologram.mt':
        # the vector-board holograms (the samurai, Greaser): their mask seen through a grid of round dots, DotsCoords of
        # them across the mask (CP2077's read like a lite-brite outline); grid rows = the dots,
        # lines-or-dots 1
        p[0] = 5.0
        p[1], p[2] = tiles or (1.0, 1.0)
        p[4], p[6], p[15] = _num(d, 'DotsCoords'), 1.0, _num(d, 'AdditiveAlphaBlend')
    else:
        return None
    return [round(x, 6) for x in p]


# effect templates with no conversion yet, only on base\fx\ meshes (the scene keeps some of those,
# cp2gltf FX_KEEP): fog lasers and light gradients (additive beams and glow cards), HUD markers, particles, liquids.
# Left out rather than drawn as metal_base's flat grey. (particles_hologram is not here: the common billboard
# holograms outside base\fx\ use it.)
FX_ONLY = {'fog_laser.mt', 'hud_markers_transparent.mt', 'light_gradients.mt', 'particles_generic.remt',
           'particles_generic_expanded.mt', 'transparent_liquid.mt', 'holo_projections.mt', 'cloth_mov.mt',
           'ice_fluid_mov.mt', 'ice_ver_mov_translucent.mt', 'emissive_basic_transparent.mt'}


PLACEHOLDER_SETUP = 'engine\\materials\\defaults\\multilayer_default.mlsetup'


def _setup_layers(root, setup):
    """A layer setup's layer count (its .mlsetup.json), or None when it is not fetched"""
    f = Path(root) / (setup.replace(SL, '/') + '.json')
    try:
        return len(json.loads(f.read_text(encoding='utf-8'))['Data']['RootChunk']['layers'])
    except (OSError, ValueError, KeyError, TypeError):
        return None


def _mask_layers(root, mlmask):
    """A layer mask's layer count (the <stem>_layers images Materials.mask reads), or None"""
    stem = mlmask.replace(SL, '/').rsplit('.', 1)[0]
    d = Path(root) / (stem + '_layers')
    return len(list(d.glob('*.png'))) if d.is_dir() else None


def _app_name(i, key):
    """an appearance's name as the Material.json keys it (its index appended), as cp2gltf.MeshLods reads it"""
    return key[:-len(str(i))] if key.endswith(str(i)) else key


_sibling_cache = {}
_name_index = {}


def _real_setup(m):
    s = ((m or {}).get('Data') or {}).get('MultilayerSetup') or ''
    return s if s and s.lower() != PLACEHOLDER_SETUP.lower() else ''


SETUPS_INDEX = 'setups_by_name.json'     # _setups_by_name's index, kept in <root>/../matcache
SETUPS_STATS = {'scans': 0, 'kept': 0}   # how the index was had in this process (cp2gltf logs them)


def _material_files(root):
    """root's .Material.json files with their sizes and times, (relative path, size, mtime_ns), sorted"""
    out = []
    for d, _, files in os.walk(root):
        for n in files:
            if n.endswith('.Material.json'):
                st = os.stat(os.path.join(d, n))
                out.append((os.path.relpath(os.path.join(d, n), root).replace(os.sep, '/'), st.st_size, st.st_mtime_ns))
    return sorted(out)


def _setups_by_name(root):
    """every material name under root with the real layer setups it is given: name -> {setup: count}. One scan of the
    .Material.json files is about two minutes (29,485 in Watson, 0.35 GB); a scene build met a placeholder in 163 of
    268 parts, each its own process. The index is kept on disk beside the converted
    materials (<root>/../matcache/setups_by_name.json), keyed on every file's path, size and time: a process stats them
    (seconds) and reads the index when nothing changed, else scans and writes it again."""
    key = str(root).lower()
    if key in _name_index:
        return _name_index[key]
    files = _material_files(root)
    stamp = hashlib.sha1(json.dumps([SETUPS_INDEX, files]).encode()).hexdigest()
    kept = Path(root).parent / 'matcache' / SETUPS_INDEX
    try:
        e = json.loads(kept.read_text(encoding='utf-8'))
        if e.get('stamp') == stamp:
            _name_index[key] = {n: c for n, c in e['index']}	# (pairs: a None name stays None)
            SETUPS_STATS['kept'] += 1
            return _name_index[key]
    except (OSError, ValueError, KeyError, TypeError):
        pass
    SETUPS_STATS['scans'] += 1
    idx = {}
    for f in sorted(Path(root) / r for r, _, _ in files):
        try:
            mats = json.loads(f.read_text(encoding='utf-8')).get('Materials') or []
        except (OSError, ValueError):
            continue
        for m in mats:
            s = _real_setup(m)
            if s:
                c = idx.setdefault(m.get('Name'), {})
                c[s] = c.get(s, 0) + 1
    _name_index[key] = idx
    try:
        kept.parent.mkdir(parents=True, exist_ok=True)
        tmp = kept.with_suffix('.tmp%d' % os.getpid())
        tmp.write_text(json.dumps({'stamp': stamp, 'index': [[n, c] for n, c in idx.items()]}), encoding='utf-8')
        os.replace(tmp, kept)
    except OSError:                 # (another process writing it: theirs stands)
        pass
    return _name_index[key]


def _common_prefix(a, b):
    n = 0
    for x, y in zip(a.lower(), b.lower()):
        if x != y:
            break
        n += 1
    return n


def placeholder_setups(root, mj, d):
    """A mesh's multilayered materials that came through on the engine's placeholder layer setup (multilayer_default:
    one layer, the DEBUG texture), given a real one. Canyon rocks drew with a DEBUG
    texture (53 of Watson's scene materials). canyon_rocks_large_a and _c have the same appearances as _b and _d, whose
    every appearance names a real ml_canyon_rocks_large_abcd* setup (the name covers all four meshes); in a and c each
    came through as the placeholder. A placeholder takes, the first that gives one (each with as many layers as the
    material's mask):
      1. the setup a material of the same name has in another mesh of the same folder;
      2. the setup a material of the same name has anywhere else (the most of them);
      3. the setup the folder's other meshes use for the same appearance, from the meshes whose names share the most
         with this one's (canyon_rocks_large_b and _d for _a);
      4. none: the material converts with no layer setup (a flat grey), never the DEBUG texture.
    d is the Material.json as loaded; its records are changed in place ('placeholder_setup' marks them). Returns the
    number changed."""
    mats = {m.get('Name'): m for m in d.get('Materials') or []}
    todo = [m for m in mats.values() if (((m.get('Data') or {}).get('MultilayerSetup') or '').lower()
                                         == PLACEHOLDER_SETUP.lower())]
    if not todo:
        return 0
    folder, me = Path(mj).parent, Path(mj).name[:-len('.Material.json')]
    key = str(folder).lower()
    if key not in _sibling_cache:     # per folder: [(mesh, {material name: setup}, {appearance: [setups]})]
        sib = []
        for f in sorted(folder.glob('*.Material.json')):
            try:
                sd = json.loads(f.read_text(encoding='utf-8'))
            except (OSError, ValueError):
                continue
            smats = {m.get('Name'): m for m in sd.get('Materials') or []}
            byname = {n: _real_setup(m) for n, m in smats.items() if _real_setup(m)}
            byapp = {}
            for i, (k, names) in enumerate((sd.get('Appearances') or {}).items()):
                for n in dict.fromkeys(names or []):
                    if _real_setup(smats.get(n)):
                        byapp.setdefault(_app_name(i, k), []).append(_real_setup(smats.get(n)))
            sib.append((f.name[:-len('.Material.json')], byname, byapp))
        _sibling_cache[key] = sib
    sib = [x for x in _sibling_cache[key] if x[0].lower() != me.lower()]

    def pick(tally, want):
        ok = {s: c for s, c in tally.items() if want is None or _setup_layers(root, s) in (None, want)}
        return max(sorted(ok), key=lambda s: ok[s]) if ok else ''

    changed = 0
    for m in todo:
        name = m.get('Name')
        want = _mask_layers(root, (m.get('Data') or {}).get('MultilayerMask') or '')
        tally = {}
        for _, byname, _ in sib:
            if byname.get(name):
                tally[byname[name]] = tally.get(byname[name], 0) + 1
        best = pick(tally, want)
        if not best:
            best = pick(dict(_setups_by_name(root).get(name) or {}), want)
        if not best:
            apps = [_app_name(i, k) for i, (k, names) in enumerate((d.get('Appearances') or {}).items()) if name in (names or [])]
            near = [(_common_prefix(me, x[0]), x) for x in sib if any(x[2].get(a) for a in apps)]
            top = max([n for n, _ in near] or [0])
            tally = {}
            for n, (_, _, byapp) in near:
                if n == top:
                    for a in apps:
                        for s in byapp.get(a, []):
                            tally[s] = tally.get(s, 0) + 1
            best = pick(tally, want)
        m.setdefault('Data', {})['MultilayerSetup'] = best
        m['placeholder_setup'] = PLACEHOLDER_SETUP
        changed += 1
    return changed


def lin(a):
    return np.where(a <= 0.04045, a / 12.92, ((a + 0.055) / 1.055) ** 2.4)


def srgb(a):
    a = np.clip(a, 0, 1)
    return np.where(a <= 0.0031308, a * 12.92, 1.055 * a ** (1 / 2.4) - 0.055)


def colour(v, default=(1.0, 1.0, 1.0)):
    """CP Color {Red..} (0-255) or Vector4 {X..} -> linear-ish 0..1 RGB as stored."""
    if not isinstance(v, dict):
        return np.array(default, np.float32)
    if 'Red' in v:
        return np.array([v.get('Red', 0), v.get('Green', 0), v.get('Blue', 0)], np.float32) / 255.0
    return np.array([v.get('X', 1.0), v.get('Y', 1.0), v.get('Z', 1.0)], np.float32)


def levels(x, li, lo):
    """CP2077's multilayer levels: li, lo the template's entries (each {'Elements': [value at white, value at black]};
    None: the identity). The input pair picks the texture's range, the output pair maps it: the templates' own
    defaults ([1, 0]) are the identity only read this way."""
    if li is not None:
        w, b = li['Elements']
        x = np.clip((x - b) / (w - b), 0.0, 1.0) if w != b else x
    if lo is not None:
        w, b = lo['Elements']
        x = b + (w - b) * x
    return x


def levels_slope(li, lo):
    """d(levels)/d(texture), sign included: the renderer's per-texel range for a tiled layer (1 without tables, 0
    where the input span is 0)."""
    s = 1.0
    if li is not None:
        w, b = li['Elements']
        if w == b:
            return 0.0
        s /= (w - b)
    if lo is not None:
        w, b = lo['Elements']
        s *= (w - b)
    return float(s)


def sample(t, uv):
    """Bilinear wrap sample of t (H, W, C) at UVs (N, N, 2)."""
    h, w = t.shape[:2]
    x = (uv[..., 0] % 1.0) * w - 0.5
    y = (uv[..., 1] % 1.0) * h - 0.5
    x0, y0 = np.floor(x).astype(int), np.floor(y).astype(int)
    fx, fy = (x - x0)[..., None], (y - y0)[..., None]
    x0, y0 = x0 % w, y0 % h
    x1, y1 = (x0 + 1) % w, (y0 + 1) % h
    return (t[y0, x0] * (1 - fx) * (1 - fy) + t[y0, x1] * fx * (1 - fy) + t[y1, x0] * (1 - fx) * fy +
            t[y1, x1] * fx * fy)


class Materials:
    SHARE_SIZE = 256        # the detail slots' share maps (start at 256 and measure)

    def __init__(self, root, size=512, layer_size=512, bake_dir=None, ml_size=None, detail_size=0, detail_slots=1):
        """bake_dir: cache of multilayer composites (.npz per setup, mask and size) across builds. ml_size: the
        composites' size (default `size`); single textures are capped at `size`. detail_size: multilayered materials
        leave their main layer to the renderer, tiled (its detail image at most this size); 0 bakes every layer.
        detail_slots: how many layer groups the renderer tiles (1: the main layer, .xdet v1; up to 8: .xdet v2, each
        with its share in a share map)."""
        self.root, self.size, self.layer_size = Path(root), size, layer_size
        self.ml_size = ml_size or size
        self.detail_size = detail_size
        self.detail_slots = max(1, min(8, int(detail_slots)))
        self.screen_sheets = False      # television_ad: the whole flipbook sheet (a renderer that plays .xscr's tiles)
        # metal_base_det.mt: its tiled detail baked in as a cutout (detail_cutout_image)
        self.detail_cutout = False
        # the collision build's kinds (collision stays independent of look fixes):
        # with collision set, every later reclassification is left out of the kind, by class: the
        # device diodes' EmissiveColor1, the placeholder TV ads' stand-ins, the detail cutouts, the placeholder layer
        # setups. The scene build keeps them all
        self.collision = False
        self.bake_dir = Path(bake_dir) if bake_dir else None
        self._bake_code = None
        self.tex_cache, self.tpl_cache, self.ml_cache, self.detail_cache = OrderedDict(), {}, OrderedDict(), {}
        self.tex_bytes = self.ml_bytes = 0
        self.ad_pool = None
        self.missing = set()
        # the files a conversion reads (track(): ResultCache keys a result on them); a memoised bake or detail image
        # keeps the files it was made from, so a later conversion that takes it from memory counts them too
        self._trk, self.ml_deps, self.detail_deps = [], {}, {}

    # ---- what a conversion read

    @contextmanager
    def track(self):
        """Within: every file a conversion reads, or looks for and misses, and the depots it found missing ->
        {'paths': set of str, 'missing': set}."""
        t = {'paths': set(), 'missing': set()}
        self._trk.append(t)
        try:
            yield t
        finally:
            self._trk.pop()             # (the innermost: trackers nest; by position, two may hold the same)

    def _touch(self, path, missing=None):
        for t in self._trk:
            if path is not None:
                t['paths'].add(str(path))
            if missing:
                t['missing'].add(missing)

    def _replay(self, deps):
        for t in self._trk:
            t['paths'] |= deps['paths']
            t['missing'] |= deps['missing']

    # ---- inputs

    def png(self, depot):
        if not depot or not isinstance(depot, str):
            return None
        return self.root / (depot.replace(SL, '/').rsplit('.', 1)[0] + '.png')

    TEX_CACHE_BYTES = 768 * 2 ** 20

    def tex(self, depot, mode='RGBA', cap=None):
        """Texture as float array (H, W, C) 0..1, or None. cap: longest side. Kept in an LRU of TEX_CACHE_BYTES: a
        scene worker held every texture it read as float32 (most of a 7-11 GB peak)."""
        key = (depot, mode, cap)
        if self._trk:
            self._touch(self.png(depot))
        if key in self.tex_cache:
            self.tex_cache.move_to_end(key)
            if self._trk and self.tex_cache[key] is None and depot:
                self._touch(None, depot)
        else:
            p = self.png(depot)
            if p is None or not p.exists():
                if depot:
                    self.missing.add(depot)
                    self._touch(None, depot)
                self.tex_cache[key] = None
            else:
                im = Image.open(p).convert(mode)
                if cap and max(im.size) > cap:
                    s = cap / max(im.size)
                    im = im.resize((max(1, round(im.size[0] * s)), max(1, round(im.size[1] * s))),
                                   Image.Resampling.LANCZOS)
                a = np.asarray(im, np.float32) / 255
                self.tex_cache[key] = a if a.ndim == 3 else a[..., None]
                self.tex_bytes += self.tex_cache[key].nbytes
                while self.tex_bytes > self.TEX_CACHE_BYTES and len(self.tex_cache) > 1:
                    _, old = self.tex_cache.popitem(last=False)
                    self.tex_bytes -= old.nbytes if old is not None else 0
        return self.tex_cache[key]

    def template(self, depot):
        f = self.root / (depot.replace(SL, '/') + '.json')
        if depot not in self.tpl_cache:
            self.tpl_cache[depot] = (json.loads(f.read_text(encoding='utf-8'))['Data']['RootChunk']
                                     if f.exists() else None)
            if not f.exists():
                self.missing.add(depot)
        self._touch(f, depot if self.tpl_cache[depot] is None else None)
        return self.tpl_cache[depot]

    def foliage_profile(self, depot):
        """A SpeedTree material's CFoliageProfile (<work>/fp/<cache name>.json, cp2gltf.fetch_serialised: the alpha cuts,
        colour gradient), or {} when there is none or it is not fetched."""
        if not isinstance(depot, str) or not depot.endswith('.fp'):
            return {}
        f = self.root.parent / 'fp' / (hashlib.sha1(depot.lower().encode()).hexdigest()[:10] + '_' + depot.split(SL)[-1]
                                       + '.json')
        if depot not in self.tpl_cache:
            self.tpl_cache[depot] = (json.loads(f.read_text(encoding='utf-8'))['Data']['RootChunk'] if f.exists() else {})
        self._touch(f, None if self.tpl_cache[depot] else depot)
        return self.tpl_cache[depot]

    def mask(self, mlmask, i, n):
        stem = mlmask.replace(SL, '/').rsplit('.', 1)[0]
        p = self.root / (stem + '_layers') / ('%s_%d.png' % (stem.rsplit('/', 1)[-1], i))
        self._touch(p)
        if not p.exists():
            return None
        im = Image.open(p).convert('L').resize((n, n), Image.Resampling.BICUBIC)
        return np.asarray(im, np.float32)[..., None] / 255

    # ---- multilayer

    @staticmethod
    def _table(tpl, key, name, default):
        for want in (name, 'null'):
            for e in (tpl.get('overrides') or {}).get(key, []):
                if e['n']['$value'] == want:
                    return e.get('v')
        return default

    def _microblend(self, L, m, uv):
        d = (L.get('microblend') or {}).get('DepotPath', {}).get('$value')
        if not d or d.replace(SL, '/').lower().endswith('/default.xbm'):
            return m
        mb = self.tex(d, 'RGBA', 256)
        a = sample(mb, uv * float(L.get('mbTile', 1.0)))[..., 3:4] if mb is not None else np.full_like(m, 0.41)
        c = float(L.get('microblendContrast', 1.0) or 0.0)
        w = 0.5 * (1.0 - min(max(c, 0.0), 1.0)) + 0.02
        x = np.clip((m - ((1.0 - a) - w)) / (2 * w), 0.0, 1.0)
        return np.minimum(m, x * x * (3 - 2 * x))

    def bake_path(self, setup, mlmask, n):
        """The bake cache's file for a composite (None without a cache)."""
        if not self.bake_dir:
            return None
        # (|lv2: the levels read as CP2077 reads them; older bakes are not reused)
        tag = '%s|%s|lv2' % (setup, mlmask) + (('|main1' if self.detail_slots == 1 else '|slots%d' % self.detail_slots)
                                               if self.detail_size else '')
        return self.bake_dir / ('%s_%d.npz' % (hashlib.sha1(tag.encode()).hexdigest()[:16], n))

    def multilayer(self, setup, mlmask, n):
        """(colour, normal, ORM, detail) bakes of a multilayered material, or None. detail: None, or the main layer the
        renderer tiles over the bake: {template, tile (repeats per UV unit), normal (strength), rough (range)}."""
        key = (setup, mlmask, n)
        if key in self.ml_cache:
            self.ml_cache.move_to_end(key)
            self._replay(self.ml_deps[key])
            return self.ml_cache[key]
        with self.track() as deps:
            disk = self.bake_path(setup, mlmask, n)
            self._touch(disk)
            got = self._load_bake(disk, self.bake_code()) if disk is not None and disk.exists() else None
            if got is None:
                got = self._multilayer(setup, mlmask, n)
                if disk is not None and got is not None:
                    disk.parent.mkdir(parents=True, exist_ok=True)
                    det = got[3]
                    extra = {}
                    if det is not None and 'share' in det:      # (the share map as an array beside the JSON)
                        extra['share'] = det['share']
                        det = {k: v for k, v in det.items() if k != 'share'}
                    # (to a name of this process's, renamed: another process never reads a half-written bake)
                    tmp = disk.with_name('%s.tmp%d.npz' % (disk.stem, os.getpid()))
                    np.savez(tmp, col=got[0], nrm=got[1], orm=got[2], detail=np.array(json.dumps(det)),
                             code=np.array(self.bake_code()[1]), **extra)
                    try:
                        os.replace(tmp, disk)
                    except OSError:         # (another process writing the same bake: theirs stands)
                        tmp.unlink()
        # kept in an LRU of ML_CACHE_BYTES (each bake is about 3 MB; what is dropped comes back from the bake cache)
        self.ml_cache[key] = got
        self.ml_deps[key] = deps
        self.ml_bytes += self._bake_bytes(got)
        while self.ml_bytes > self.ML_CACHE_BYTES and len(self.ml_cache) > 1:
            old_key, old = self.ml_cache.popitem(last=False)
            self.ml_deps.pop(old_key, None)
            self.ml_bytes -= self._bake_bytes(old)
        return got

    ML_CACHE_BYTES = 768 * 2 ** 20

    @staticmethod
    def _bake_bytes(got):
        if got is None:
            return 0
        share = got[3].get('share') if got[3] is not None else None
        return sum(getattr(x, 'nbytes', 0) for x in got[:3]) + getattr(share, 'nbytes', 0)

    # the bake computation's code key (bake_code) from the lv2 bakes (2026-11-06) until bakes held their own key: a bake
    # file without one was made by this code (the key was the same at every commit of cp77mat.py in that time)
    LEGACY_BAKE_CODE = 'fc93196e5ec2b415'

    def bake_code(self):
        """(code, key): the code key of what computes a bake (_multilayer and what it reaches, stagekey.py) and the bake's
        key, that code with the sizes it reads; a bake file holds its key, one made otherwise is baked again"""
        if self._bake_code is None:
            import stagekey
            code = stagekey.code_key(Path(__file__).resolve(), ['Materials._multilayer'], entry=None)
            key = hashlib.sha1(json.dumps([code, self.layer_size, self.detail_size, self.detail_slots]).encode())
            self._bake_code = (code, key.hexdigest()[:16])
        return self._bake_code

    _stale_said = False

    @classmethod
    def _load_bake(cls, disk, want):
        """a cached bake, or None when the file does not read (cut short by a stopped run) or another code or other sizes
        made it (want: bake_code()): baked again, in its place"""
        import zipfile
        try:
            with np.load(disk) as z:
                ok = str(z['code']) == want[1] if 'code' in z.files else want[0] == cls.LEGACY_BAKE_CODE
                if not ok:
                    if not cls._stale_said:
                        print('cp77mat: bake %s was made by other code or sizes: baked again (and others like it)'
                              % disk.name, flush=True)
                        cls._stale_said = True
                    return None
                detail = json.loads(str(z['detail'])) if 'detail' in z.files else None
                if detail is not None and 'share' in z.files:
                    detail['share'] = z['share']
                return z['col'], z['nrm'], z['orm'], detail
        except (OSError, ValueError, KeyError, EOFError, zipfile.BadZipFile) as e:
            print('cp77mat: bake %s does not read (%s): baked again' % (disk.name, e), flush=True)
            return None

    @staticmethod
    def _tdep(tpl, k):
        return (tpl.get(k) or {}).get('DepotPath', {}).get('$value')

    def _multilayer(self, setup, mlmask, n):
        f = self.root / (setup.replace(SL, '/') + '.json')
        self._touch(f, None if f.exists() else setup)
        if not f.exists():
            self.missing.add(setup)
            return None
        layers = json.loads(f.read_text(encoding='utf-8'))['Data']['RootChunk']['layers']
        v, u = np.mgrid[0:n, 0:n].astype(np.float32)
        uv = np.stack([(u + 0.5) / n, (v + 0.5) / n], -1)
        col = np.full((n, n, 3), 0.18, np.float32)
        rough = np.full((n, n, 1), 0.6, np.float32)
        metal = np.zeros((n, n, 1), np.float32)
        slope = np.zeros((n, n, 2), np.float32)

        # each layer's coverage (its mask by its opacity, the microblend breaking the edge) and template
        cover = []
        for i, L in enumerate(layers):
            op = float(L.get('opacity', 1.0))
            m = None if op <= 0 else np.ones((n, n, 1), np.float32) if i == 0 else self.mask(mlmask, i, n)
            if m is not None and i > 0:
                m = self._microblend(L, m, uv)
            tpl = self.template(L['material']['DepotPath']['$value']) if m is not None and (m * op).max() >= 1e-3 else None
            cover.append((m * op, tpl) if tpl is not None else None)
        # the main layer: the one whose tiling shows most, by how much of the surface it shows on (under none of the
        # layers above it) and how much its texture varies (a fine paint barely changes a pixel; asphalt does). It
        # shows on a fifth of the surface at least.
        main, shown = None, None
        slots, slot_of, best_of = [], {}, {}
        if self.detail_size and self.detail_slots > 1:
            # .xdet v2: the layers grouped by (template, tile), each
            # group's visible share summed; the top detail_slots by share times texture variation (as the main layer
            # is chosen) with at least 0.5% of the surface. The renderer tiles each where its share map says; the bake
            # keeps their mean colour and a flat slope
            above, groups = np.ones((n, n, 1), np.float32), {}
            for i in reversed(range(len(layers))):
                if cover[i] is None:
                    continue
                A, tpl = cover[i]
                vis = A * above
                above = above * (1 - A)
                if not (self._tdep(tpl, 'colorTexture') or self._tdep(tpl, 'normalTexture')):
                    continue
                tile = float(layers[i].get('matTile', 1.0)) * float(tpl.get('tilingMultiplier', 1.0))
                g = groups.setdefault((layers[i]['material']['DepotPath']['$value'], round(tile, 4)),
                                      {'share': np.zeros((n, n, 1), np.float32), 'layers': [], 'best': (-1.0, None)})
                g['share'] += vis
                g['layers'].append(i)
                if float(vis.mean()) > g['best'][0]:
                    g['best'] = (float(vis.mean()), i)
            ranked = sorted(((float(g['share'].mean()) * (0.02 + self.detail_energy(k[0])), k, g)
                             for k, g in groups.items() if float(g['share'].mean()) >= 0.005),
                            key=lambda e: -e[0])[:self.detail_slots]
            for s, (_, k, g) in enumerate(ranked):
                slots.append({'template': k[0], 'share': g['share']})
                for i in g['layers']:
                    slot_of[i] = s
                best_of[g['best'][1]] = s
        elif self.detail_size:
            above, best = np.ones((n, n, 1), np.float32), 0.0
            for i in reversed(range(len(layers))):
                if cover[i] is None:
                    continue
                A, tpl = cover[i]
                vis = A * above
                above = above * (1 - A)
                if vis.mean() < 0.2 or not (self._tdep(tpl, 'colorTexture') or self._tdep(tpl, 'normalTexture')):
                    continue
                score = float(vis.mean()) * (0.02 + self.detail_energy(layers[i]['material']['DepotPath']['$value']))
                if score > best:
                    main, shown, best = i, vis, score

        detail = None
        for i, L in enumerate(layers):
            if cover[i] is None:
                continue
            A, tpl = cover[i]
            tile = float(L.get('matTile', 1.0)) * float(tpl.get('tilingMultiplier', 1.0))
            uvt = uv * tile
            ls = self.layer_size

            def tdep(k):
                return self._tdep(tpl, k)
            ct = self.tex(tdep('colorTexture'), 'RGB', ls)
            tint = np.array(self._table(tpl, 'colorScale', L['colorScale']['$value'],
                                        {'Elements': [0.5, 0.5, 0.5]})['Elements'], np.float32)
            tiled = i == main or i in slot_of
            if tiled:
                # the renderer tiles this layer: its mean here (a constant over the surface)
                c = np.clip((2.0 * lin(ct).reshape(-1, 3).mean(0) if ct is not None else np.ones(3, np.float32)) * tint,
                            0.0, 1.0)
            else:
                c = np.clip((2.0 * lin(sample(ct, uvt)) if ct is not None else np.ones((n, n, 3), np.float32)) * tint,
                            0.0, 1.0)
            rt = self.tex(tdep('roughnessTexture'), 'L', ls)
            if tiled:
                rs = np.full((n, n, 1), float(rt.mean()) if rt is not None else 0.5, np.float32)
            else:
                rs = sample(rt, uvt) if rt is not None else np.full((n, n, 1), 0.5, np.float32)
            # CP2077's levels, in and out, each pair [value at white, value at black]; the template's own 'null' entry
            # too (asphalt_old_01's is not the identity). The output pair was once read reversed and the
            # input pair ignored: Misty's street baked at roughness 0.28 where CP2077's is 0.41
            ri = self._table(tpl, 'roughLevelsIn', L['roughLevelsIn']['$value'], None)
            ro = self._table(tpl, 'roughLevelsOut', L['roughLevelsOut']['$value'], None)
            r = levels(rs, ri, ro)
            mt = self.tex(tdep('metalnessTexture'), 'L', ls)
            ms = sample(mt, uvt) if mt is not None else np.zeros((n, n, 1), np.float32)
            mi = self._table(tpl, 'metalLevelsIn', L['metalLevelsIn']['$value'], None)
            mo = self._table(tpl, 'metalLevelsOut', L['metalLevelsOut']['$value'], None)
            me = levels(ms, mi, mo)
            col = col * (1 - A) + c * A
            rough = rough * (1 - A) + r * A
            metal = metal * (1 - A) + me * A
            nt = self.tex(tdep('normalTexture'), 'RGB', ls)
            ns = L.get('normalStrength')
            ns = ns.get('$value') if isinstance(ns, dict) else ns
            k = float(self._table(tpl, 'normalStrength', ns or 'null', 1.0) or 0.0)
            if tiled:
                slope = slope * (1 - A)     # flat here: the renderer adds the tiled bumps
                d = {'template': L['material']['DepotPath']['$value'], 'tile': tile,
                     'normal': k if nt is not None else 0.0,
                     'rough': levels_slope(ri, ro)}
                if i == main:
                    detail = d
                elif i in best_of:          # a slot's settings: its most visible layer's
                    slots[best_of[i]].update(d)
            elif nt is not None and k > 0:
                s = sample(nt, uvt)
                slope = slope * (1 - A) + np.stack([s[..., 0] * 2 - 1, -(s[..., 1] * 2 - 1)], -1) * k * A
        nrm = np.concatenate([slope, np.ones((n, n, 1), np.float32)], -1)
        nrm /= np.linalg.norm(nrm, axis=-1, keepdims=True)
        # ORM's red: occlusion (none: 1), or with a main layer where it shows (v1; v2 has its share maps)
        occ = shown if shown is not None else np.ones_like(rough)
        if slots:
            # the share maps: slot s in channel s % 4 of map s // 4, raw shares (not sRGB), at SHARE_SIZE; an empty
            # slot's channel 0
            sh = np.zeros((n, n, 8), np.float32)
            for s, e in enumerate(slots):
                sh[..., s] = e.pop('share')[..., 0]
            m = self.SHARE_SIZE
            if n != m:
                sh = (sh.reshape(m, n // m, m, n // m, 8).mean((1, 3)) if n % m == 0 else
                      np.stack([np.asarray(Image.fromarray(sh[..., c], 'F').resize((m, m), Image.Resampling.BILINEAR))
                                for c in range(8)], -1))
            # (rounded down: the channels' sum stays within 255, as the renderer weighs them)
            detail = {'slots': slots, 'share': np.uint8(np.floor(np.clip(sh, 0, 1) * 255 + 1e-4))}
        return (np.uint8(np.round(srgb(col) * 255)), np.uint8(np.round((nrm * 0.5 + 0.5) * 255)),
                np.uint8(np.round(np.clip(np.concatenate([occ, rough, metal], -1), 0, 1) * 255)), detail)

    def detail_energy(self, template):
        """How much a template's tiling varies: its brightness's spread over its mean plus its normal's mean slope."""
        d = self.detail_image(template).astype(np.float32) / 255.0
        return float((d[..., 2] * 2.0).std() + np.abs(d[..., :2] * 2.0 - 1.0).mean())

    def detail_image(self, template):
        """A layer template's tiling texture as zero-mean detail (RGBA uint8, at most detail_size square): rg its
        normal's slope (OpenGL, as the bakes), b its brightness over its mean / 2, a its roughness over its mean + 0.5."""
        if template in self.detail_cache:
            self._replay(self.detail_deps[template])
            return self.detail_cache[template]
        with self.track() as deps:
            got = self._detail_image(template)
        self.detail_deps[template] = deps
        return got

    def _detail_image(self, template):
        tpl = self.template(template)
        n = self.detail_size
        ct = self.tex(self._tdep(tpl, 'colorTexture'), 'RGB', n) if tpl else None
        nt = self.tex(self._tdep(tpl, 'normalTexture'), 'RGB', n) if tpl else None
        rt = self.tex(self._tdep(tpl, 'roughnessTexture'), 'L', n) if tpl else None
        s = max([t.shape[0] for t in (ct, nt, rt) if t is not None] + [4])
        s = 1 << (s.bit_length() - 1)

        def fit(t):
            if t.shape[:2] == (s, s):
                return t
            ch = [np.asarray(Image.fromarray(np.float32(t[..., c]), 'F').resize((s, s), Image.Resampling.BILINEAR))
                  for c in range(t.shape[2])]
            return np.stack(ch, -1)
        out = np.full((s, s, 4), 0.5, np.float32)
        if nt is not None:
            t = fit(nt)
            out[..., 0] = t[..., 0]
            out[..., 1] = 1.0 - t[..., 1]          # DirectX green down -> up
        if ct is not None:
            lum = (lin(fit(ct)) * np.array([0.2126, 0.7152, 0.0722], np.float32)).sum(-1)
            out[..., 2] = lum / max(float(lum.mean()), 1e-4) * 0.5
        if rt is not None:
            t = fit(rt)[..., 0]
            out[..., 3] = t - float(t.mean()) + 0.5
        self.detail_cache[template] = got = np.uint8(np.round(np.clip(out, 0, 1) * 255))
        return got

    # ---- single-texture materials

    def rgba(self, depot, tint=None, cap=None):
        t = self.tex(depot, 'RGBA', cap or self.size)
        if t is None:
            return None
        if tint is None:
            return np.uint8(np.round(np.clip(t, 0, 1) * 255))
        # tex() holds 8-bit images (k / 255), so the tint takes 256 values a channel: the same srgb(lin(x) * tint),
        # stored as float32 as the image was, for each, then looked up (52x, the same bytes)
        x = np.arange(256, dtype=np.float32)[:, None] / 255
        v = np.empty((256, 3), np.float32)
        v[:] = srgb(lin(np.broadcast_to(x, (256, 3))) * tint)
        lut = np.uint8(np.round(np.clip(v, 0, 1) * 255))
        u8 = np.uint8(np.round(np.clip(t, 0, 1) * 255))
        out = np.empty(u8.shape, np.uint8)
        for c in range(3):
            out[..., c] = lut[u8[..., c], c]
        out[..., 3:] = u8[..., 3:]
        return out

    @staticmethod
    def screen_intensity(d):
        """Screens glow at EmissiveEV on the lamps' scale (2^(EV - 5), EV 5 -> 1)."""
        return float(np.clip(2.0 ** (first_ev(d, ('EmissiveEV',), 3.5) - 5.0), 0.1, 16.0))

    def ad_frame(self, depot, tiles_w=1, tiles_h=1, frame=0):
        """One frame of an ad sheet (TilesWidth x TilesHeight frames, row-major from the top left)."""
        img = self.rgba(depot, cap=2048)
        if img is None:
            return None
        tiles_w, tiles_h = max(int(tiles_w), 1), max(int(tiles_h), 1)     # some data says 0 (one frame)
        if tiles_w > 1 or tiles_h > 1:
            h, w = img.shape[:2]
            fw, fh = w // max(tiles_w, 1), h // max(tiles_h, 1)
            x, y = (frame % tiles_w) * fw, (frame // tiles_w % tiles_h) * fh
            img = img[y:y + fh, x:x + fw]
        img = img.copy()
        img[..., 3] = 255
        return img

    def neon_parallax_image(self, d):
        """A neon_parallax sign's face: its BaseColor (a logo, often grey) through its GradientMap when
        UseGradientMapMode is set (the luminance picks the gradient's colour along its middle row, times the
        luminance), scaled by
        BaseColorScale; None without a BaseColor. Drawn as a flat white glow before"""
        img = self.rgba(d.get('BaseColor'))
        if img is None or self.is_default(d.get('BaseColor')):
            return None
        rgb = img[..., :3].astype(np.float32) / 255.0
        if d.get('UseGradientMapMode') and d.get('GradientMap'):
            g = self.rgba(d.get('GradientMap'))
            if g is not None:
                row = g[g.shape[0] // 2, :, :3].astype(np.float32) / 255.0
                lum = rgb.mean(2)
                # the BaseColor is a mask (a logo in grey): the gradient colours it, the mask keeps its shape (a white
                # gradient made the whole face white)
                rgb = row[np.clip(np.round(lum * (len(row) - 1)).astype(np.int64), 0, len(row) - 1)] * lum[..., None]
        sc = d.get('BaseColorScale') or {}
        rgb = rgb * np.array([float(sc.get(k, 1.0)) for k in 'XYZ'], np.float32)
        out = img.copy()
        out[..., :3] = np.uint8(np.round(np.clip(rgb, 0, 1) * 255))
        return out

    def gradient_recolor_image(self, d):
        """metal_base_gradientmap_recolor's colour: its BaseColor is a grey mask (a billboard's logo and lettering)
        and its GradientMap a ramp, the mask's value picking the ramp's colour along its middle row (the whole face
        coloured, the background too: the Red Room board's pink panel, its white letters). None without both."""
        img = self.rgba(d.get('BaseColor'))
        g = self.rgba(d.get('GradientMap'))
        if img is None or g is None or self.is_default(d.get('BaseColor')) or self.is_default(d.get('GradientMap')):
            return None
        row = g[g.shape[0] // 2, :, :3].astype(np.float32) / 255.0
        lum = img[..., :3].astype(np.float32).mean(2) / 255.0
        rgb = row[np.clip(np.round(lum * (len(row) - 1)).astype(np.int64), 0, len(row) - 1)]
        sc = d.get('BaseColorScale') or {}
        rgb = rgb * np.array([float(sc.get(k, 1.0)) for k in 'XYZ'], np.float32)
        out = img.copy()
        out[..., :3] = np.uint8(np.round(np.clip(rgb, 0, 1) * 255))
        return out

    def ad_image(self, path):
        """a rendered CP2077 ad (look/ads-20261105/rendered: the ad's ink layout, straight alpha) as a screen's face:
        over black, opaque, at most 2048 px wide"""
        from PIL import Image
        # tracked as every image a conversion reads is (tex, rgba): a re-render under the same name misses the result
        # cache, where before the old face was kept
        self._touch(path)
        try:
            im = Image.open(path).convert('RGBA')
        except OSError:
            return None
        if im.width > 2048:
            im = im.resize((2048, max(1, round(im.height * 2048 / im.width))), Image.Resampling.LANCZOS)
        a = np.asarray(im).astype(np.float32)
        out = np.empty(a.shape, np.uint8)
        out[..., :3] = np.uint8(np.round(a[..., :3] * (a[..., 3:] / 255.0)))
        out[..., 3] = 255
        return out

    def find_ad_pool(self):
        """Every television_ad AdTexture under root with its image present: [(depot, tiles_w, tiles_h)]. Reads every
        .Material.json (about 50 s on the slice's folder, cold); a tiled build finds it once (cp2gltf.py tiles)."""
        pool = set()
        for f in sorted(self.root.rglob('*.Material.json')):
            try:
                mats = json.loads(f.read_text(encoding='utf-8')).get('Materials') or []
            except (OSError, ValueError):
                continue
            for m in mats:
                dd = m.get('Data') or {}
                if m.get('MaterialTemplate', '').lower().endswith('television_ad.mt') and dd.get('AdTexture'):
                    pool.add((dd['AdTexture'], int(dd.get('TilesWidth') or 1), int(dd.get('TilesHeight') or 1)))
        # a sheet some material reads whole (1 x 1) and another as frames: the frames (ads_set_2x4 whole drew eight
        # ads side by side on one billboard)
        most = {}
        for depot, tw, th in pool:
            most[depot] = max(most.get(depot, 1), max(tw, 1) * max(th, 1))
        pool = {p for p in pool if max(p[1], 1) * max(p[2], 1) == most[p[0]]}
        return sorted(p for p in pool if self.png(p[0]) and self.png(p[0]).exists() and self.real_ad(*p))

    AD_NOT = (r'engine\textures\editor',)
    # the editor folder as a path prefix; the rest as whole words of the path (between \ / _ . or the ends), so an ad
    # named e.g. "metropolis" or a folder "gradients_pack" isn't taken for one (not substrings anywhere)
    AD_NOT_RE = re.compile(r'(?:^|[\\/_.])(?:color_checker|gradient|flipbook|metro|mask|streamers)(?=$|[\\/_.])')
    # where ads live: the advertising folders and the quests' own billboards (Gibson, The Passion). television_ad also
    # plays steam and fire flipbooks, the crossing signals' WALK/WAIT, curb info strips and vending machine fronts;
    # in the pool they stood in for billboards (smoke on the Japantown ring ads, WALK on
    # a billboard)
    AD_ROOTS = ('base\\environment\\decoration\\advertising\\', 'base\\environment\\decoration\\unique\\quest\\')

    def real_ad(self, depot, tw, th):
        """An ad worth showing: from an advertising or quest billboard folder, not an editor or test texture (a flat
        grey or white, CP's colour checker, gradients, LED flipbooks, metro line screens, masks, cloth banners) and a
        first frame of at least 128 px with visible content. 14 of Watson's 72 pool entries were such and drew flat
        magenta and blue billboards"""
        low = depot.lower()
        if not low.startswith(self.AD_ROOTS) or low.startswith(self.AD_NOT[0]) or self.AD_NOT_RE.search(low):
            return False
        img = self.ad_frame(depot, tw, th, 0)
        if img is None or min(img.shape[:2]) < 128:
            return False
        lum = img[..., :3].astype(np.float32).mean(2)
        return float(lum.std()) >= 12.0

    def standin_ad(self, key):
        """A frame of one of the area's own ads (every television_ad AdTexture under root), chosen by key."""
        if self.ad_pool is None:
            self.ad_pool = self.find_ad_pool()
        if not self.ad_pool:
            return None
        h = int(hashlib.sha1(key.encode()).hexdigest()[:8], 16)
        depot, tw, th = self.ad_pool[h % len(self.ad_pool)]
        return self.ad_frame(depot, tw, th, (h >> 8) % max(tw * th, 1))

    DETAIL_CUT_SIZE = 1024      # detail_cutout_image's bake, a side
    DETAIL_CUT_SUB = 4          # its subsamples a side per texel

    def detail_cutout_image(self, d, base, thr):
        """metal_base_det.mt's tiled detail baked in (chainlink fences, grates and vents
        drew as solid sheets: 432 of Watson's scene materials). CP2077 tiles DetailColor DetailU x DetailV times over the
        UV and cuts the surface where its alpha is under AlphaThreshold; the base colour (BaseColor x BaseColorScale) is
        under it. Baked at DETAIL_CUT_SIZE: the colour the base times the detail's (both sRGB images, multiplied in
        linear), the alpha the share of each texel's DETAIL_CUT_SUB^2 subsamples whose detail alpha passes the threshold
        (so the renderer's cut at one half keeps a wire at least half a texel wide; its coverage-keeping mips do the
        rest), the normal the base's with the tiled DetailNormal (whiteout blend). (RGBA uint8, normal RGB uint8 or
        None), or None when the detail is not fetched."""
        det = self.rgba(d.get('DetailColor'), cap=4096)
        if det is None:
            return None
        U = float(d.get('DetailU') or 1.0) or 1.0
        V = float(d.get('DetailV') or 1.0) or 1.0
        S, k = self.DETAIL_CUT_SIZE, self.DETAIL_CUT_SUB
        dh, dw = det.shape[:2]
        n = S * k
        ix = np.minimum((((np.arange(n) + 0.5) / n * U) % 1.0 * dw).astype(np.int64), dw - 1)
        iy = np.minimum((((np.arange(n) + 0.5) / n * V) % 1.0 * dh).astype(np.int64), dh - 1)
        cut = int(np.ceil(thr * 255.0))
        dn = self.normal_map(d.get('DetailNormal'))
        cov = np.empty((S, S), np.float32)
        drgb = np.empty((S, S, 3), np.float32)
        dnrm = np.empty((S, S, 3), np.float32) if dn is not None else None
        if dn is not None:
            nh, nw = dn.shape[:2]
            jx = np.minimum((((np.arange(n) + 0.5) / n * U) % 1.0 * nw).astype(np.int64), nw - 1)
            jy = np.minimum((((np.arange(n) + 0.5) / n * V) % 1.0 * nh).astype(np.int64), nh - 1)
        band = 64
        for r0 in range(0, S, band):
            rows = iy[r0 * k:(r0 + band) * k]
            sub = det[rows][:, ix]                              # (band k, n, 4) uint8
            b = sub.shape[0] // k
            cov[r0:r0 + b] = (sub[..., 3] >= cut).reshape(b, k, S, k).mean((1, 3))
            drgb[r0:r0 + b] = lin(sub[..., :3].astype(np.float32) / 255.0).reshape(b, k, S, k, 3).mean((1, 3))
            if dn is not None:
                sn = dn[jy[r0 * k:(r0 + band) * k]][:, jx].astype(np.float32) / 127.5 - 1.0
                dnrm[r0:r0 + b] = sn.reshape(b, k, S, k, 3).mean((1, 3))
        bimg = base if base.shape[:2] == (S, S) else np.asarray(Image.fromarray(base).resize((S, S), Image.BILINEAR))
        col = srgb(lin(bimg[..., :3].astype(np.float32) / 255.0) * drgb)
        out = np.empty((S, S, 4), np.uint8)
        out[..., :3] = np.uint8(np.round(np.clip(col, 0, 1) * 255))
        out[..., 3] = np.uint8(np.round(np.clip(cov, 0, 1) * 255))
        nb = self.normal_map(d.get('Normal'))
        if nb is not None and nb.shape[:2] != (S, S):
            nb = np.asarray(Image.fromarray(nb).resize((S, S), Image.BILINEAR))
        if dnrm is None and nb is None:
            return out, None
        if dnrm is None:
            return out, nb
        if nb is None:
            m = dnrm
        else:   # whiteout: the slopes added, the heights multiplied
            a = nb.astype(np.float32) / 127.5 - 1.0
            m = np.stack([a[..., 0] + dnrm[..., 0], a[..., 1] + dnrm[..., 1], a[..., 2] * dnrm[..., 2]], -1)
        m /= np.maximum(np.linalg.norm(m, axis=-1, keepdims=True), 1e-6)
        return out, np.uint8(np.round(np.clip(m * 127.5 + 127.5, 0, 255)))

    def is_default(self, depot):
        return not depot or depot.startswith(DEFAULTS) or depot.lower().endswith(
            (SL + 'black.xbm', SL + 'white.xbm', SL + 'grey.xbm', SL + 'normal.xbm'))

    def normal_map(self, depot):
        """CP normal maps store XY (DirectX, green down); rd-vulkan wants OpenGL RGB."""
        if self.is_default(depot):
            return None
        t = self.tex(depot, 'RGB', self.size)
        if t is None:
            return None
        x, y = t[..., 0] * 2 - 1, -(t[..., 1] * 2 - 1)
        z = np.sqrt(np.clip(1 - x * x - y * y, 0, 1))
        return np.uint8(np.round(np.stack([x, y, z], -1) * 127.5 + 127.5))

    def convert(self, mat):
        if self.collision and mat.get('placeholder_setup'):
            # (collision: the record as it came, its placeholder setup put back: placeholder_setups is a look fix)
            mat = dict(mat, Data=dict(mat.get('Data') or {}, MultilayerSetup=mat['placeholder_setup']))
        t = mat['MaterialTemplate'].split(SL)[-1].lower()
        d = mat.get('Data') or {}
        base = mat.get('BaseMaterial', '')
        ex = {'cp_template': t, 'cp_material': base if base.endswith('.mi') else mat['Name']}
        out = {'kind': 'opaque', 'image': None, 'normal': None, 'orm': None, 'extras': ex}

        if t in FX_ONLY:
            return dict(out, kind='skip')
        if t.startswith('multilayered'):
            got = self.multilayer(d.get('MultilayerSetup', ''), d.get('MultilayerMask', ''), self.ml_size)
            if got is None:
                out['image'] = np.full((4, 4, 4), 128, np.uint8)
                return out
            col, nrm, orm, detail = got
            out.update(image=np.concatenate([col, np.full(col.shape[:2] + (1,), 255, np.uint8)], -1),
                       normal=nrm, orm=orm, detail=detail)
            return out

        if t in ('neon_tubes.mt', 'neon_parallax.mt'):
            # at EmissiveEV on the lamps' scale like the other glows (2^(EV - 5)); none given: 1
            ev = d.get('EmissiveEV')
            img = self.neon_parallax_image(d) if t == 'neon_parallax.mt' else None
            if img is not None:
                out['image'] = img
            out.update(kind='emissive', extras=dict(ex, lp_emissive={
                'color': colour(d.get('color'), (1, 1, 1)).tolist(),
                'intensity': 1.0 if ev is None else float(np.clip(2.0 ** (float(ev) - 5.0), 0.1, 16.0)), 'tint': True,
                'light': False}))
            return out
        if t == 'signages.mt':
            # at EmissiveEV on the lamps' scale (2^(EV - 5)), capped at 1: every sign used to glow at 1, its
            # "off" and "dim" appearances too (EV 0-2.5: 542 of the 2,807 materials). Signs over EV 5 stay at 1 here;
            # brighter is the renderer's look choice
            col = colour(d.get('ColorOneStart'), (1, 1, 1))
            ev = d.get('EmissiveEV')
            img = self.rgba(d.get('MainTexture'))
            if img is not None and not int(_num(d, 'UniformColor', default=1.0)) and isinstance(d.get('ColorOneEnd'), dict):
                # not one colour: ColorOneStart to ColorOneEnd across the sign (along U, or V with
                # HorizontalOrVerticalGradient 1, from GradientStartPosition over 1 / ColorGradientScale), baked into the
                # mask, which then glows in white. Every such sign used to glow in its start colour alone: 116
                # of Watson's scene materials (El Coyote's Kiroshi ring red where CP2077's is magenta)
                h, w = img.shape[:2]
                vert = int(_num(d, 'HorizontalOrVerticalGradient')) == 1
                t = (np.arange(h if vert else w, dtype=np.float32) + 0.5) / (h if vert else w)
                t = np.clip((t - _num(d, 'GradientStartPosition')) * _num(d, 'ColorGradientScale', default=1.0), 0.0, 1.0)
                ramp = col[None] + (colour(d['ColorOneEnd'])[None] - col[None]) * t[:, None]
                ramp = ramp[:, None, :] if vert else ramp[None, :, :]
                img = img.copy()
                img[..., :3] = np.uint8(np.round(srgb(lin(img[..., :3].astype(np.float32) / 255.0) * ramp) * 255))
                col = np.ones(3, np.float32)
            out.update(kind='emissive', image=img,
                       extras=dict(ex, lp_emissive={'color': col.tolist(), 'intensity': 1.0 if ev is None else float(
                           np.clip(2.0 ** (float(ev) - 5.0), 0.1, 1.0)), 'tint': True, 'light': False}))
            return out
        if t == 'television_ad.mt':
            # an ad (the ring, digital billboards): AdTexture, a sheet of TilesWidth x TilesHeight frames played in
            # turn; the first frame stands for the loop, or with screen_sheets the whole sheet (the renderer plays it
            # by .xscr's tiles and frames per second: the Kabuki ring's ads cycle in CP2077)
            adt = str(d.get('AdTexture') or '').lower()
            if not self.collision and ('color_checker' in adt or adt.startswith('engine' + SL + 'textures' + SL +
                                                                                'editor')):
                # a placeholder AdTexture (the colour checker: 29 of Watson's 57 television_ad screens, billboards and
                # the Corpo Plaza roundabout's long screen): CP2077 fills the screen with an ad at run time, so as the UI
                # screens below, CP2077's own for it (cp77_ads) or one of the area's. The checker's
                # sixteen colour tiles used to draw stretched over the screen (a pink, violet
                # and red strip by the roundabout). Only the checker and the editor's textures: the metro line signs,
                # LED flipbooks and gradient signs are the screens' own content
                v = mat.get('_variant', '')
                img = self.ad_image(v[4:]) if v.startswith('|ad:') else self.standin_ad(base + '|' + mat['Name'] + v)
                if img is None:
                    return dict(out, kind='skip')
                out.update(kind='emissive', image=img, extras=dict(ex, standin_ad=True, screen=screen_params(t, d, (1.0, 1.0)),
                                                                   lp_emissive={'color': [1, 1, 1], 'intensity': self.screen_intensity(d),
                                                                                'tint': False, 'light': False}))
                return out
            tw, th = max(int(d.get('TilesWidth') or 1), 1), max(int(d.get('TilesHeight') or 1), 1)
            if getattr(self, 'screen_sheets', False) and (tw > 1 or th > 1):
                img = self.rgba(d.get('AdTexture'), cap=4096)
                if img is not None:
                    img = img.copy()
                    img[..., 3] = 255
                tiles = (float(tw), float(th))
            else:
                img = self.ad_frame(d.get('AdTexture'), tw, th)
                tiles = (1.0, 1.0)
            if img is None:
                return dict(out, kind='skip')
            out.update(kind='emissive', image=img, extras=dict(ex, screen=screen_params(t, d, tiles), lp_emissive={
                'color': [1, 1, 1], 'intensity': self.screen_intensity(d), 'tint': False, 'light': False}))
            return out
        if t in ('metal_base_ui.mt', 'parallaxscreen_transparent_ui.mt'):
            # screens showing a UI the game renders at run time (ad billboards, terminals): the data holds only a
            # placeholder, so one of the area's own ads stands in, the same one for a material (and the scene build's
            # per-placement variant, '_variant') every build
            v = mat.get('_variant', '')
            # CP2077's own ad for this screen (cp77_ads.AdMap: '|ad:<rendered png>'), else a stand-in from the pool
            img = self.ad_image(v[4:]) if v.startswith('|ad:') else self.standin_ad(base + '|' + mat['Name'] + v)
            if img is None:
                return dict(out, kind='skip')
            tint = colour(d.get('Tint'), (1, 1, 1)) if t.endswith('transparent_ui.mt') else np.ones(3, np.float32)
            em = {'color': tint.tolist(), 'intensity': self.screen_intensity(d), 'tint': True, 'light': False}
            sp = screen_params(t, d)
            if t.endswith('transparent_ui.mt'):
                # holographic panels: translucent, glowing (blend kind with emission)
                img = img.copy()
                img[..., 3] = 150
                out.update(kind='blend', image=img, extras=dict(ex, lp_emissive=em, standin_ad=True, screen=sp))
            else:
                out.update(kind='emissive', image=img, extras=dict(ex, lp_emissive=em, standin_ad=True, screen=sp))
            return out
        if t in ('parallaxscreen.mt', 'parallaxscreen_transparent.mt', 'screen.mt',
                 'screen_fluff.mt', 'hologram.mt', 'hologram_two_sided.mt'):
            depot = d.get('ParalaxTexture') or d.get('ImageAtlas') or d.get('Albedo') or d.get('MainTexture')
            img = self.rgba(depot)
            if img is None:
                return dict(out, kind='skip')
            # (the image is the texture whole: a parallax flipbook's sheet already, its tiles in .xscr)
            out.update(kind='emissive', image=img, extras=dict(ex, screen=screen_params(t, d), lp_emissive={
                'color': [1, 1, 1], 'intensity': 0.5, 'tint': False, 'light': False}))
            return out
        if t == 'particles_hologram.mt':
            # the common billboard holograms (vector boards: Greaser, Vatnajokull, 24/7, Tyger Claws tags): AlphaMask, a
            # grey mask, glowing in ColorParam x ColorMultiplier. They used to fall to metal_base below, flat
            # grey panels (71 of Watson's materials: the tall towers). Drawn
            # translucent and black where the mask says, glowing there (glow_mask); ColorMultiplier 150-200 read as
            # / 100 on the lamps' scale (a bright neon). The dots and glitch are not carried.
            mask = self.rgba(d.get('AlphaMask'))
            if mask is None:
                return dict(out, kind='skip')
            # the mask squared before the mips (the strokes kept, the grey painted fill between them gone: the samurai's
            # interior is a 0.36 median grey) and ColorMultiplier / 50: one calibration for every hologram (dawn and night
            # references), r_sceneHoloGain 2 and r_sceneHoloCurve 2
            img = np.zeros(mask.shape, np.uint8)
            a = mask[..., :3].max(-1).astype(np.float32) / 255.0
            img[..., 3] = np.uint8(np.round(a * a * 255))
            out.update(kind='blend', image=img, extras=dict(ex, glow_mask=True, screen=screen_params(t, d), lp_emissive={
                'color': colour(d.get('ColorParam')).tolist(),
                'intensity': float(np.clip(_num(d, 'ColorMultiplier', default=100.0) / 50.0, 0.5, 8.0)), 'tint': True,
                'light': False}))
            return out
        if t == 'window_parallax_interior.mt':
            # rooms behind the glass (scene.frag's interior mapping): RoomAtlas holds pre-rendered rooms side by side
            # (a "_2x1" atlas wide ones); the facade is cut into rooms of roomWidth x roomHeight metres, roomDepth deep,
            # AmountTurnOffAtNight of them dark, the rest lit in TintColorAtNight at EmissiveEV with their colour
            # temperature spread by LightsTempVariationAtNight
            img = self.rgba(d.get('RoomAtlas'))
            name = str(d.get('RoomAtlas') or '').lower()
            tiles = 0
            if img is not None:
                tiles = max(1, int(round(img.shape[1] / img.shape[0] / (2.0 if '_2x1' in name else 1.0))))
            ev = d.get('EmissiveEV')
            ev = 4.0 if ev is None else float(ev)
            col = colour(d.get('TintColorAtNight'), (1.0, 1.0, 1.0))
            room = [float(d.get('roomWidth') or 3.0), float(d.get('roomHeight') or 4.0), float(d.get('roomDepth') or 4.0)]
            out.update(kind='emissive', image=img, extras=dict(ex, lp_emissive={
                'color': col.tolist(), 'intensity': float(np.clip(2.0 ** (ev - 5.0), 0.0, 16.0)), 'tint': True, 'light': False},
                window={'room': room, 'off': float(d.get('AmountTurnOffAtNight') or 0.0),
                        'temp': float(d.get('LightsTempVariationAtNight') or 0.0), 'tiles': tiles}))
            return out
        if t.startswith('glass') or t in ('window_glass.mt',):
            tint = colour(d.get('TintColor'), (0.75, 0.8, 0.85))
            img = np.zeros((8, 8, 4), np.uint8)
            img[..., :3] = np.uint8(np.round(srgb(tint * 0.25) * 255))
            img[..., 3] = 90
            out.update(kind='blend', image=img)
            return out
        if t.startswith('mesh_decal') or t.startswith('decal') or 'blendable_mesh_decal' in t:
            depot = d.get('DiffuseTexture')
            tint = colour(d.get('DiffuseColor'), (1, 1, 1))
            img = self.rgba(depot, tint)
            if img is None or self.is_default(depot):
                return dict(out, kind='skip')
            # (an explicit 0 is a normal-only decal: kept, as cp77decals keeps it; absent is 1)
            alpha = 1.0 if d.get('DiffuseAlpha') is None else float(d.get('DiffuseAlpha'))
            if t.startswith('mesh_decal'):
                # a projected mesh decal (drawn as a 0.7 blend surface, the wall
                # behind it showed through with its own light): CP2077 writes its albedo at DiffuseAlpha and its
                # normal at NormalAlpha into the surface behind, within DepthThreshold. The renderer's DBuffer reads
                # these (scene flags bit 6); an older one draws it as before, the alpha applied through the tint
                # (cp2gltf scene_part), so the image keeps its own alpha here
                nrm = self.normal_map(d.get('NormalTexture'))
                height = None if self.is_default(d.get('HeightTexture')) else self.rgba(d.get('HeightTexture'))
                out.update(kind='blend', image=img, extras=dict(out.get('extras') or {}, decal={
                    'alpha': alpha, 'normal_alpha': float(d.get('NormalAlpha') or 0.0),
                    'depth': float(d.get('DepthThreshold') if d.get('DepthThreshold') is not None else 0.5),
                    'height_strength': float(d.get('HeightStrength') or 0.0),
                    'normal': nrm, 'height': height}))
                return out
            if alpha < 1.0:
                img[..., 3] = np.uint8(img[..., 3] * alpha)
            out.update(kind='blend', image=img)
            return out
        if t.startswith('speedtree') or t.startswith('vegetation'):
            # SpeedTree materials name their image DiffuseMap (611 of Watson's: every tree drew invisible); the stand-in when there is none is opaque, so the cutout test keeps the tree
            depot = d.get('DiffuseTexture') or d.get('DiffuseMap') or d.get('Diffuse') or d.get('BaseColor')
            img = self.rgba(depot)
            if img is None:
                img = np.full((4, 4, 4), 90, np.uint8)
                img[..., 3] = 255
            else:
                # the leaves' alpha cut is the FoliageProfile's (cutoffAlphaMinMip at full size, billboardCutoffAlpha
                # on a billboard), not the renderer's 0.5: the alpha remapped so CP2077's cut lands on 0.5 (niwaki cuts
                # at 0.28: at 0.5 the pines lost most of their needles)
                fp = self.foliage_profile(d.get('FoliageProfile'))
                cut = float(fp.get('billboardCutoffAlpha' if t.endswith('billboard.mt') else 'cutoffAlphaMinMip') or 0.0)
                if 0.02 < cut < 0.98 and abs(cut - 0.5) > 0.01:
                    a = img[..., 3].astype(np.float32) / 255.0
                    a = np.where(a < cut, a * (0.5 / cut), 0.5 + (a - cut) * (0.5 / (1.0 - cut)))
                    img = img.copy()
                    img[..., 3] = np.uint8(np.round(np.clip(a, 0, 1) * 255))
            out.update(kind='cutout', image=img)
            if t == 'speedtree_3d_v8_twosided.mt':
                # leaf cards: the renderer (H19's r_sceneFoliage) lights the canopy as one mass, normals pulled toward
                # the mesh's bounding-sphere direction (the scene's flag 0x800; per card they lit leaf by leaf)
                out['extras'] = dict(ex, foliage_leaves=True)
            return out
        if t.startswith(('fillable_fluid', 'shadows', 'invisible', 'proxy')) or 'invisible' in base.lower():
            return dict(out, kind='skip')    # chunk_invisible.mi: mesh chunks the appearance hides

        if t == 'lights_interactive.mt' and mat.get('_zone') is not None:
            # a signal head's lit zone (cp2gltf splits its faces by their vertex colour, zone = round(5 c) - 1): the
            # LED dot mask (Emissive, repeated LayerTile times) glowing at Zone<n>EmissiveEV. The game's controller
            # picks the lit lamp at run time; the rest of the face falls through below and stays dark (crossing signals
            # showed no WALK / DON'T WALK)
            z = int(mat['_zone'])
            img = self.rgba(d.get('Emissive')) if isinstance(d.get('Emissive'), str) else None
            if img is None:
                img = np.full((4, 4, 4), 255, np.uint8)
            tile = max(1, min(int(round(float(d.get('LayerTile') or 1))), 16))
            img = np.tile(img, (tile, tile, 1))
            img[..., 3] = 255
            ev = first_ev(d, ('Zone%dEmissiveEV' % z, 'EmissiveEV'), 8.0)
            lamp = mat.get('_lamp') or 'red'
            col = LAMP_COLOURS['walk' if lamp == 'green' and mat.get('_crossing') else lamp]
            out.update(kind='emissive', image=img, extras=dict(ex, signal_lamp=lamp, lp_emissive={
                'color': list(col), 'intensity': float(np.clip(2.0 ** (ev - 5.0), 0.1, 16.0)), 'tint': True,
                'light': False}))
            return out
        if t == 'lights_interactive.mt' and isinstance(d.get('Emissive'), str) and d['Emissive'].lower().endswith(
                ('white.xbm', 'small_white.xbm')):
            # a lamp's bulb, lens or LED with no zone split (a work light's bulb, wall and street lamps, door LEDs):
            # the whole surface glows at zone 0's EV. No EmissiveColor here (the light controller colours it at run
            # time), so white. Before, these fell to metal_base below, opaque white and unlit: 179 of Watson's
            # materials, ~24,900 instances (lights from these
            # props but the prop itself had no bulb). Masked ones (7) still fall through.
            ev = first_ev(d, ('Zone0EmissiveEV', 'EmissiveEV'), 5.0)
            out.update(kind='emissive', extras=dict(ex, lp_emissive={
                'color': colour(d.get('EmissiveColor')).tolist(), 'intensity': float(np.clip(2.0 ** (ev - 5.0), 0.1, 16.0)),
                'tint': True, 'light': False}))
            return out
        # metal_base and its relatives: BaseColor x BaseColorScale, roughness/metal maps, emissive
        em = d.get('Emissive') if isinstance(d.get('Emissive'), str) else ''
        em, ec = em.lower(), d.get('EmissiveColor') or {}
        if t == 'device_diode.mt' and not ec and not self.collision and \
                _num(d, 'EmissiveInitialState', default=1.0) > 0:
            # a device's lamp or diode (corridor and bar lamps, LED strips) names its colour EmissiveColor1 (the second
            # only for UseTwoEmissiveColors' switching), lit while EmissiveInitialState is on: it used to
            # fall through here without a glow, opaque and unlit (El Coyote's red pool-table lamp)
            ec = d.get('EmissiveColor1') or {}
        if t == 'metal_base_gradientmap_recolor.mt' and (ec.get('Red') or ec.get('Green') or ec.get('Blue')):
            # vector billboards (Red Room, Greaser, Peep Show...): the mask through the ramp, glowing at EmissiveEV in
            # EmissiveColor. The whole-surface branch below drew them as blank white panels (63 of night_city_b's
            # materials; the CP2077 reference at Misty's, look/cp77ref-20261104)
            img = self.gradient_recolor_image(d)
            if img is not None:
                ev = float(d.get('EmissiveEV') or 0.0)
                img[..., 3] = 255
                # vector_board: drawn as a screen (material flag 0x200), so the renderer's screen gain lights it as
                # CP2077 does its ad screens (the Red Room board 168 73 78 against CP2077's 204 142
                # 151 at EmissiveEV 3.5 in white; 198 103 108 as a screen)
                out.update(kind='emissive', image=img, extras=dict(ex, vector_board=True, lp_emissive={
                    'color': colour(ec).tolist(), 'intensity': float(np.clip(2.0 ** (ev - 5.0), 0.1, 16.0)),
                    'tint': True, 'light': False}))
                return out
        if (em and not em.endswith('black.xbm') and (ec.get('Red') or ec.get('Green') or ec.get('Blue'))
                and (em.endswith(('white.xbm', 'small_white.xbm')) or SL + 'editor' + SL in em)):
            # the whole surface glows (lamp lenses, LED strips, light panels): EmissiveColor x 2^EmissiveEV, with
            # EV 5 (the median in Kabuki) as the neon tubes' intensity 1. Masked glows are not carried yet.
            ev = float(d.get('EmissiveEV') or 0.0)
            out.update(kind='emissive', extras=dict(ex, lp_emissive={
                'color': colour(ec).tolist(), 'intensity': float(np.clip(2.0 ** (ev - 5.0), 0.1, 16.0)), 'tint': True,
                'light': False}))
            return out
        depot = (d.get('BaseColor') or d.get('DiffuseTexture') or d.get('Diffuse') or d.get('Albedo') or
                 d.get('MainTexture') or d.get('ColorTexture'))
        tint = colour(d.get('BaseColorScale') or d.get('DiffuseColor') or d.get('Color'), (1, 1, 1))
        img = self.rgba(depot, tint) if depot and not self.is_default(depot) else None
        if img is None:
            # the engine's placeholder textures by their own value (white 1, grey 0.5 sRGB, black 0); none: 0.18
            low = (depot or '').lower()
            base_v = (1.0 if low.endswith(('white.xbm', 'small_white.xbm')) else 0.214 if low.endswith('grey.xbm')
                      else 0.0 if low.endswith('black.xbm') else 0.18)
            img = np.full((4, 4, 4), 255, np.uint8)
            img[..., :3] = np.uint8(np.round(srgb(np.clip(tint * base_v, 0, 1)) * 255))
        thr = float(d.get('AlphaThreshold') or 0.0)
        if self.detail_cutout and not self.collision and t == 'metal_base_det.mt' and thr > 0 and \
                not self.is_default(d.get('DetailColor')):
            got = self.detail_cutout_image(d, img, thr)
            if got is not None:
                out.update(kind='cutout', image=got[0], normal=got[1])
                return out
        if thr > 0 and img.shape[0] > 4:
            a = img[..., 3]
            if (a < 128).mean() > 0.02 and (a >= 128).mean() > 0.02:
                out['kind'] = 'cutout'
        if out['kind'] == 'opaque':
            img[..., 3] = 255
        out['image'] = img
        out['normal'] = self.normal_map(d.get('Normal'))
        if (t == 'metal_base_proxy.mt' and em and not em.endswith('black.xbm') and em != (depot or '').lower()
                and (ec.get('Red') or ec.get('Green') or ec.get('Blue'))):
            # a far proxy's glow (billboards: the baked ad; buildings: lit windows): its Emissive is a grey mask over the
            # baked BaseColor, which glows in its own colours there (the far ads
            # bright and in colour). The mask rides in the normal image's alpha (extras glow_albedo: the scene's flag
            # 0x400), so a cutout keeps its alpha; a cutout proxy (344 of Watson's 390 billboard
            # proxies) used not to glow at all and an opaque one glowed white
            mask = self.rgba(d.get('Emissive'))
            if mask is not None:
                ev = float(d.get('EmissiveEV') or 0.0)
                ga = mask[..., :3].max(-1)
                if re.search(r'billboard|advert|commercial|holo', em) and 0 < int(ga.max()) < 255:
                    # a billboard proxy's mask is the screen's coverage, baked at 128-160 on a third of Watson's: scaled
                    # to full at its peak, as the screen itself glows (NEW JOB muted at 0.5). A building's lit windows keep their own levels
                    ga = np.uint8(np.round(ga.astype(np.float32) * (255.0 / int(ga.max()))))
                out['extras'] = dict(ex, glow_albedo=ga, lp_emissive={
                    'color': colour(ec).tolist(), 'intensity': float(np.clip(2.0 ** (ev - 5.0), 0.1, 16.0)), 'tint': True,
                    'light': False})
                return out
        if out['kind'] == 'opaque' and em and not em.endswith('black.xbm') and (ec.get('Red') or ec.get('Green') or
                                                                               ec.get('Blue')):
            # a masked glow (paper lanterns, lightbox faces, banner ads) over the lit surface: Emissive x EmissiveColor
            # x 2^(EmissiveEV - 5). A mask that is the colour texture itself glows in the surface's own colours; any
            # other rides in the colour image's alpha (extras glow_mask: the scene's flag 4) and glows in EmissiveColor
            mask = None
            if em != (depot or '').lower():
                mask = self.rgba(d.get('Emissive'))
                if mask is None:
                    return out
                m = lin(mask[..., :3].astype(np.float32) / 255.0).max(-1)
                if img.shape[0] <= 4:       # a flat placeholder colour: take the mask's size
                    img = np.ascontiguousarray(np.broadcast_to(img[:1, :1], m.shape + (4,)))
                    out['image'] = img
                if m.shape != img.shape[:2]:
                    m = np.asarray(Image.fromarray(np.float32(m), 'F').resize(img.shape[1::-1], Image.BILINEAR))
                img[..., 3] = np.uint8(np.round(np.clip(m, 0, 1) * 255))
            ev = float(d.get('EmissiveEV') or 0.0)
            out['extras'] = dict(ex, glow_mask=mask is not None, lp_emissive={
                'color': colour(ec).tolist(), 'intensity': float(np.clip(2.0 ** (ev - 5.0), 0.1, 16.0)), 'tint': True,
                'light': False})
        return out


class ResultCache:
    """What a conversion gave a caller, on disk across processes, tiles and tags (convert
    was 65% of a scene part, and one tag rebuilt 90 parts for 73 new bakes). An entry is keyed on the material record,
    the converter's settings, its code (stagekey), numpy's and Pillow's versions, the ad pool and the caller's salt
    (the caller's own code and what it keeps), and holds the size and time of every file the conversion read or looked
    for (Materials.track): one changed, gone or newly there since, and the entry misses. Off without an ad pool (a
    stand-in ad would find one by reading every material: a tile's part or merge has the tiles stage's)."""

    def __init__(self, folder, mats, salt=''):
        import PIL
        import stagekey
        self.dir, self.mats = Path(folder), mats
        code = stagekey.code_key(Path(__file__).resolve(), ['Materials'], entry=None)
        self.base = json.dumps([code, salt, mats.size, mats.layer_size, mats.ml_size, mats.detail_size, np.__version__,
                                PIL.__version__] + ([mats.detail_slots] if mats.detail_slots > 1 else [])
                               + (['screen_sheets'] if getattr(mats, 'screen_sheets', False) else [])
                               + (['detail_cutout'] if getattr(mats, 'detail_cutout', False) else [])
                               + (['collision'] if getattr(mats, 'collision', False) else []))
        self.hits = self.misses = 0

    def path(self, rec):
        if self.mats.ad_pool is None:
            return None
        pool = hashlib.sha1(json.dumps(self.mats.ad_pool).encode()).hexdigest()
        k = hashlib.sha1((self.base + pool + json.dumps(rec, sort_keys=True)).encode()).hexdigest()[:24]
        return self.dir / k[:2] / (k + '.json')

    @staticmethod
    def _stat(path):
        try:
            st = os.stat(path)
            return [st.st_size, st.st_mtime_ns]
        except OSError:
            return None

    def get(self, rec):
        """The value kept for rec, its missing inputs added to the converter's (as converting would), or None."""
        f = self.path(rec)
        try:
            e = json.loads(f.read_text(encoding='utf-8')) if f is not None else None
        except (OSError, ValueError):
            e = None
        if e is None or any(self._stat(p) != s for p, s in e['deps']):
            self.misses += 1
            return None
        self.mats.missing.update(e['missing'])
        self.hits += 1
        return e['value']

    def put(self, rec, value, deps):
        """Keep value (JSON) for rec, read from deps (a track() result, taken after the conversion and anything the
        caller made from it); False when it is not JSON or there is no key."""
        f = self.path(rec)
        if f is None:
            return False
        try:
            text = json.dumps({'deps': sorted([p, self._stat(p)] for p in deps['paths']),
                               'missing': sorted(deps['missing']), 'value': value})
        except (TypeError, ValueError):
            return False
        f.parent.mkdir(parents=True, exist_ok=True)
        tmp = f.with_suffix('.tmp%d' % os.getpid())
        tmp.write_text(text, encoding='utf-8')
        try:
            os.replace(tmp, f)
        except OSError:                 # (another process writing the same entry: theirs stands)
            tmp.unlink()
        return True
