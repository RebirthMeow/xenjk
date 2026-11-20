"""w3fx.py -- Witcher 3 water particle effects as a render scene's maps/<map>.xfx (Beauclair).

The waterfalls' splashes, mist, crest and landing foam, rings, the fountains' jets and splashes and the rivers' splash
spots, drawn by the renderer's particle path (r_sceneFx, the format cp77fx.py writes for Night City; its writer is
reused). Inputs:
- the systems' emitters (w3fx_dump.py, inside Blender with the Witcher 3 add-on): drawer, birth rate, the most
  particles, material, texture, atlas. The add-on cannot read the cooked emitter modules (lifetime, size, velocity,
  colour over life come back as its defaults), so those are set here per kind of effect (KINDS: hand-set from the
  game's look, not read from the data);
- the placements: the world's sector particle objects (the city export drops them; the waterfalls data report's
  sector parse, particles_all.json: path, position, rotation rows = the matrix's columns, emission scale).
Writes <out>/<map>.xfx, .xfx.json and zz_<map>_fx.pk3 (the .xfx and the flipbooks, white sheets with the game texture's
shape in their alpha: its splash, foam and ring textures are normal maps with the mask in alpha). The flipbooks are
read from the add-on's uncook folder (--w3 <research>/uncook, else $W3_UNCOOK or $W3_RESEARCH/uncook: w3paths.py).
The placements' box and origin are the build record's (w3build.py's <name>.build.json), or both given by hand.
    python tools/levelport/w3fx.py --dump DUMP.json --placements particles_all.json --out DIR [--map w3_city]
        (--build-json BUILD.json | --box "x0 y0 x1 y1" --origin "x y z")"""
import argparse
import collections
import json
import sys
import zipfile
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import cp2gltf  # noqa: E402
import cp77fx  # noqa: E402
import pk3write  # noqa: E402
import w3paths  # noqa: E402
from cp77fx import (F_ADDITIVE, F_ORIENTED, F_SPAWN_BOX, F_SPAWN_CIRCLE, F_SPAWN_SPHERE, F_HEMI_UP, F_VEL_WORLD, F_HAS_ACCEL,  # noqa: E402
                    N, xfx_blob, tex_name)

UNCOOK_RESEARCH = None      # (--w3: a research folder whose uncook/ is the add-on's; None: w3paths' variables)
# the reflective mode (renderer flag 32768, scene.frag: the image's normal map catches the sky and the sun, as the
# game's water_splash_additive does with its cube): the water kinds; their flipbooks keep the normal map (a separate
# image, tagged in its key)
F_NORMAL = 32768
NORMAL_TAG = '#n'
F_ACCEL_WORLD = 65536    # (renderer: the acceleration in world space, the velocity in the emitter's)
NORMAL_KINDS = ('splash', 'small_splash', 'droplets', 'foam', 'rings', 'stream')
WHITE = [2.4, 2.5, 2.6]     # (additive: the game's splashes shine by their normal map's reflections, reflection
                            # multiplier 25-60, which this particle path does not draw)
# per kind of effect: life (s), size (m, start), size over life (multiplier, 8 samples), alpha over life, velocity range
# (m/s, world: x y z min, max), acceleration (m/s2), spawn radius (m), spin (turns/s), additive, colour (linear)
KINDS = {
    'splash': dict(body=[0.35] * 3, life=(0.8, 1.4), size=(1.2, 2.4), size_life=[0.6, 0.85, 1.0, 1.15, 1.3, 1.4, 1.5, 1.6],
                   alpha_life=[0.0, 0.9, 1.0, 0.8, 0.55, 0.35, 0.15, 0.0], vel=((-0.6, -0.6, 0.4), (0.6, 0.6, 1.6)),
                   accel=(0, 0, -3.0), spawn=0.4, spin=(-0.15, 0.15), additive=True, colour=WHITE),
    # (a fountain's jets: 20 a fountain, each tip a few droplets that barely leave it; wider, the bowl sprayed like a
    # lawn sprinkler)
    'droplets': dict(body=[0.35] * 3, life=(0.4, 0.7), size=(0.15, 0.3), size_life=[1.0] * N,
                     alpha_life=[0.0, 1.0, 1.0, 0.9, 0.7, 0.5, 0.25, 0.0], vel=((-0.3, -0.3, 0.1), (0.3, 0.3, 0.6)),
                     accel=(0, 0, -9.8), spawn=0.05, spin=(-0.3, 0.3), additive=True, colour=WHITE),
    'small_splash': dict(body=[0.35] * 3, life=(0.5, 0.8), size=(0.35, 0.7), size_life=[0.6, 0.8, 1.0, 1.15, 1.3, 1.4, 1.5, 1.6],
                         alpha_life=[0.0, 0.9, 1.0, 0.8, 0.55, 0.35, 0.15, 0.0], vel=((-0.2, -0.2, 0.1), (0.2, 0.2, 0.5)),
                         accel=(0, 0, -3.0), spawn=0.1, spin=(-0.15, 0.15), additive=True, colour=WHITE),
    # (the mist: thin and near the fall; at half alpha and 3-6 m it veiled the palace's courtyard)
    # (a jet's arc: its trail drawn as many small droplets along the same path, 5x the trail's birth rate)
    'stream': dict(body=[0.35] * 3, life=(1.0, 1.2), size=(0.12, 0.18), size_life=[1.0] * N,
                   alpha_life=[0.9, 1.0, 1.0, 1.0, 0.95, 0.85, 0.6, 0.0], vel=((0, 0, 0), (0, 0, 0)),
                   accel=(0, 0, -5.0), spawn=0.0, spin=(0, 0), additive=True, colour=WHITE),
    'mist': dict(life=(4.0, 6.0), size=(2.0, 3.5), size_life=[0.6, 0.8, 1.0, 1.15, 1.3, 1.45, 1.6, 1.75],
                 alpha_life=[0.0, 0.12, 0.18, 0.16, 0.12, 0.08, 0.04, 0.0], vel=((-0.2, -0.2, 0.1), (0.2, 0.2, 0.4)),
                 accel=(0, 0, 0), spawn=0.6, spin=(-0.03, 0.03), additive=False, colour=[0.82, 0.86, 0.9]),
    'foam': dict(body=[0.45] * 3, life=(2.0, 3.5), size=(1.5, 3.0), size_life=[0.7, 0.9, 1.1, 1.3, 1.5, 1.7, 1.85, 2.0],
                 alpha_life=[0.0, 0.7, 0.8, 0.7, 0.55, 0.4, 0.2, 0.0], vel=((-0.25, -0.25, 0.0), (0.25, 0.25, 0.0)),
                 accel=(0, 0, 0), spawn=0.8, spin=(-0.05, 0.05), additive=False, colour=[0.92, 0.95, 0.96], flat=True),
    'rings': dict(body=[0.25] * 3, life=(1.5, 2.2), size=(0.5, 0.9), size_life=[0.3, 0.8, 1.3, 1.8, 2.3, 2.7, 3.1, 3.5],
                  alpha_life=[0.0, 0.8, 0.7, 0.55, 0.4, 0.25, 0.1, 0.0], vel=((0, 0, 0), (0, 0, 0)), accel=(0, 0, 0),
                  spawn=0.6, spin=(0, 0), additive=False, colour=[0.92, 0.95, 0.96], flat=True),
}


def kind_of(path, em):
    """The kind an emitter is drawn as (None: left out)"""
    tex = (em.get('texture') or '').lower()
    name = path.lower()
    if 'Trail' in (em.get('drawer') or ''):
        return 'stream'                 # (a fountain's jet: the game's trail ribbon, drawn as a dense droplet stream)
    if 'blood' in tex or 'Mesh' in (em.get('drawer') or ''):
        return None                     # (mesh drawers and a red debug copy: not drawn)
    if 'smoke' in tex:
        return 'mist'
    if 'water_circle' in tex:
        return 'rings'
    if 'foam' in tex:
        return 'foam'
    if 'fountain_tip' in name and 'splash' not in name:
        return 'droplets'
    if 'fountain' in name:              # (a fountain's bowl: fountain_tip_splash*, p_splash_circle)
        return 'small_splash'
    return 'splash'


def flipbook(tex):
    """A white sheet with the texture's shape in its alpha (the water textures: a normal map, the mask in alpha; the
    two-channel smoke: its red the shading, its green the density)"""
    from PIL import Image
    normal = tex.endswith(NORMAL_TAG)
    tex = tex[:-len(NORMAL_TAG)] if normal else tex
    p = Path(w3paths.uncook(UNCOOK_RESEARCH)) / (tex.replace('\\', '/')[:-4] + '.dds')
    if not p.exists():
        return None
    im = Image.open(p)
    a = np.asarray(im.convert('RGBA'))
    s = max(a.shape[:2])
    if s > 1024:                        # (half or quarter size: a few particles a pixel each, sharp enough)
        f = 1024 / s
        a = np.asarray(Image.fromarray(a).resize((int(a.shape[1] * f), int(a.shape[0] * f)), Image.Resampling.BOX))
    out = np.empty(a.shape, np.uint8)
    if im.mode == 'RGB':                # (two-channel smoke: density in green, 0 at the cells' edges; its shading in red,
        # 129 at the edges: as the opacity it drew each puff as a half-opaque square)
        out[..., :3] = a[..., 0:1]
        out[..., 3] = a[..., 1]
    elif 'smoke' in tex.lower():
        out[...] = a
    elif normal:                        # (the reflective mode: the game's normal map kept, the mask softened)
        out[..., :3] = a[..., :3]
        out[..., 3] = soft_mask(a[..., 3])
    else:                               # (the mask's square root: the sparse splash masks, mean 9/255, read as faint dots)
        out[..., :3] = 255
        out[..., 3] = soft_mask(a[..., 3])
    return out


def soft_mask(alpha):
    """The game's sparse water masks (mean 9-19 of 255) made fuller: the image's background level (its median) taken
    off first, then the square root. (Without the background off, the rings' and foam's faint floor, about 0.07, came
    up to 0.27: their flat quads drew as blue squares on the cascade's pool.)"""
    a = alpha.astype(np.float32) / 255.0
    bg = float(np.median(a))
    a = np.clip((a - bg) / max(1.0 - bg, 1e-3), 0.0, 1.0)
    return np.uint8(np.round(np.sqrt(a) * 255.0))


def emitter_record(path, em, kind, textures):
    k = KINDS[kind]
    cols, rows = em.get('atlas') or [1, 1]
    normal = kind in NORMAL_KINDS
    key = (em['texture'] + (NORMAL_TAG if normal else ''), int(cols), int(rows), F_ADDITIVE if k['additive'] else 0)
    if key not in textures:
        textures[key] = len(textures)
    flags = (F_ADDITIVE if k['additive'] else 0) | F_SPAWN_SPHERE | F_HEMI_UP | F_VEL_WORLD
    flags |= F_HAS_ACCEL if any(k['accel']) else 0
    flags |= F_ORIENTED if k.get('flat') or 'EmitterOrientation' in (em.get('drawer') or '') else 0
    flags |= F_NORMAL if normal else 0
    # (reflective: the dark colour its body, the light its reflection gain, from the material's reflection multiplier:
    # the game's 15-60 against its caustics cube)
    params = em.get('params') or {}
    gain = float(np.clip(float(params.get('reflection_multiplier') or 25.0) / 16.0, 0.8, 4.0))
    dark, light = (k['body'], [gain] * 3) if normal else (k['colour'], k['colour'])
    rate = float(em.get('birth_rate') or 0.0)
    if rate <= 0.0:                     # (a burst emitter: its count over a life)
        rate = max(1, int(em.get('max_particles') or 3)) / k['life'][1]
    sz = k['size']
    r = {'texture': textures[key], 'flags': flags, 'max_particles': int(em.get('max_particles') or 55),
         'duration': 1.0, 'birth': [rate] * N, 'life': k['life'], 'size_x': sz, 'size_y': sz,
         'size_life': list(k['size_life']), 'alpha': (1.0, 1.0), 'alpha_life': list(k['alpha_life']),
         'rot': (0.0, 1.0), 'rot_rate': k['spin'], 'rot_rate_life': [1.0] * N,
         'spawn_radius': (0.0, k['spawn']), 'spawn_box': (0.0, 0.0, 0.0), 'spawn_force': (0.0, 0.0),
         'pos_min': (0.0, 0.0, 0.0), 'pos_max': (0.0, 0.0, 0.0), 'vel_min': k['vel'][0], 'vel_max': k['vel'][1],
         'vel_life': [1.0] * N, 'accel': k['accel'], 'drag': [0.0] * N,
         'anim_fps': 0.0, 'frame': (0.0, float(cols * rows) - 0.01), 'fade_near': (0.0, 0.0), 'fade_far': (0.0, 0.0),
         'dark': dark, 'light': light, 'max_alpha': 2.0, 'soft': 1.0, 'wind': 0.0, 'lod_distance': 0.0,
         # (max alpha 2: r_sceneFxAlpha halves it)
         'stretch': 0.0, 'name': '%s %s' % (path.split('\\')[-1], em.get('name'))}
    r.update(UNUSED_FIELDS)
    return r


# cp77fx.EMITTER's VelocityTurbulize, Noise and SizeByDistance fields: off (no turbulence, no noise, size 1 at every
# distance), as cp77fx.emitter_record sets them for an emitter without those modules
UNUSED_FIELDS = {'turb_min': (0.0, 0.0, 0.0), 'turb_max': (0.0, 0.0, 0.0), 'turb_interval': 0.0, 'turb_life': 0.0,
                 'noise_amp': (0.0, 0.0, 0.0), 'noise_freq': 0.0, 'noise_rate': (0.0, 0.0, 0.0),
                 'noise_amp_life': (0.0, 0.0, 0.0), 'size_near': (0.0, 0.0, 1.0), 'size_far': (0.0, 0.0, 1.0)}


def decoded_dropped(d):
    """What of a decoded emitter (w3emitter.py) the record does not carry, as stats keys: an absolute speed curve
    (written as a multiplier), a start/end pair over the emitter's time (used as a random range), the acceleration and
    colour curves over life, a second cooked module of a type, a buffer no module fits"""
    out = []
    if d.get('velocity_life') and d.get('velocity_life_mode') == 'absolute':
        out.append('velocity_life_absolute')
    for name in ('life', 'size', 'alpha', 'rotation', 'rotation_rate'):
        if d.get(name) and d.get(name + '_random') is False:
            out.append(name + '_start_end')
    for name in ('accel_life', 'color_life', 'stacked', 'undecoded'):
        if d.get(name):
            out.append(name)
    return out


def decoded_record(r, d, kind):
    """The game's own values (w3emitter.py: the cooked emitter modules) over a hand-set record r, field by field where
    the module exists: life, size and its curve, alpha and its curve, rotation and its rate, velocity (the emitter's
    space: the decoder's 'world' bit reads as the opposite, a waterfall's mist leaving at 6-9 m/s along +x) and its
    curve, acceleration, spawn shape, flipbook speed and first frame, colour, birth rate, the most particles."""
    def pair(v, i=None):
        if v is None:
            return None
        a, b = v
        return (float(a if i is None else a[i]), float(b if i is None else b[i]))
    if d.get('life'):
        r['life'] = pair(d['life'])
    if d.get('size'):
        r['size_x'], r['size_y'] = pair(d['size'], 0), pair(d['size'], 1)
    if d.get('size_life'):
        r['size_life'] = [float(s[0] if isinstance(s, list) else s) for s in d['size_life']]
    if d.get('alpha'):
        r['alpha'] = pair(d['alpha'])
    if d.get('alpha_life'):
        r['alpha_life'] = [float(x) for x in d['alpha_life']]
    if d.get('rotation'):
        r['rot'] = pair(d['rotation'])
    if d.get('rotation_rate'):
        r['rot_rate'] = pair(d['rotation_rate'])
    r['flags'] &= ~(F_VEL_WORLD | F_HAS_ACCEL | F_SPAWN_SPHERE | F_HEMI_UP)
    v = d.get('velocity')
    r['vel_min'], r['vel_max'] = (tuple(map(float, v[0])), tuple(map(float, v[1]))) if v else ((0, 0, 0), (0, 0, 0))
    if d.get('velocity_life'):
        r['vel_life'] = [float(s[0] if isinstance(s, list) else s) for s in d['velocity_life']]
    acc = d.get('accel')
    r['accel'] = tuple(map(float, acc)) if acc else (0.0, 0.0, 0.0)
    r['flags'] |= F_HAS_ACCEL if acc and any(acc) else 0
    r['flags'] |= F_ACCEL_WORLD if acc and d.get('accel_world') else 0
    sp = d.get('spawn') or {}
    r['spawn_radius'], r['spawn_box'], r['spawn_force'] = (0.0, 0.0), (0.0, 0.0, 0.0), (0.0, 0.0)
    if sp.get('shape') == 'box' and sp.get('extents'):
        r['flags'] |= F_SPAWN_BOX
        r['spawn_box'] = tuple(float(x) for x in sp['extents'][1])
    elif sp.get('shape') in ('circle', 'sphere'):
        r['flags'] |= F_SPAWN_CIRCLE if sp['shape'] == 'circle' else F_SPAWN_SPHERE
        sides = sp.get('sides') or {}
        if sp['shape'] == 'sphere' and sides and not sides.get('+z', True) and not sides.get('-z', True):
            r['flags'] = (r['flags'] & ~F_SPAWN_SPHERE) | F_SPAWN_CIRCLE      # (no z sides: a ring in xy)
        elif sp['shape'] == 'sphere' and sides and not sides.get('-z', True):
            r['flags'] |= F_HEMI_UP
        r['spawn_radius'] = (float((sp.get('inner_radius') or [0])[0]), float((sp.get('outer_radius') or [0])[0]))
        if sp.get('velocity') and sp.get('force'):
            r['spawn_force'] = pair(sp['force'])
    an = d.get('anim') or {}
    if an.get('speed'):
        sp_ = float(an['speed'][0])
        r['anim_fps'] = sp_ if an.get('mode') == 'speed' else sp_ / max(sum(r['life']) / 2.0, 1e-3)
    if d.get('frame'):
        r['frame'] = pair(d['frame'])
    if d.get('color') and kind not in NORMAL_KINDS:
        c = [float(x) for x in d['color'][0]]
        r['dark'] = r['light'] = c
    br = d.get('birth_rate')
    if br:
        r['birth'] = [float(sum(br)) / 2.0] * N
    if d.get('max_particles'):
        r['max_particles'] = int(d['max_particles'])
    if kind == 'stream':                # (the ribbon's width over its path: droplets an eighth of it, 5x as many)
        w = r['size_x'][1]
        r['size_x'] = r['size_y'] = (w * 0.25, w * 0.35)
        r['birth'] = [b * 5.0 for b in r['birth']]
        r['max_particles'] = int(r['max_particles']) * 5
        r['alpha_life'] = list(KINDS['stream']['alpha_life'])
    r['decoded'] = True
    return r


def build(dump, placements, out, mapname, box, origin, extra=None, decoded=None):
    stats = collections.Counter()
    textures, emitters, systems, sys_index = {}, [], [], {}
    dec_by = {k.lower(): v for k, v in (decoded or {}).items()}
    for path, sysd in dump.items():
        first = len(emitters)
        dems = (dec_by.get(path.lower()) or {}).get('emitters') or []
        for i, em in enumerate(sysd.get('emitters') or []):
            kind = kind_of(path, em)
            if kind is None or not em.get('texture'):
                stats['emitters_left_out'] += 1
                continue
            if kind == 'stream':
                em = dict(em, texture=r'fx\textures\water\splash_with_normal.xbm', atlas=[1, 1])
            r = emitter_record(path, em, kind, textures)
            d = dems[i] if i < len(dems) and dems[i].get('name') == em.get('name') else None
            if d is not None and d.get('tail_ok'):
                r = decoded_record(r, d, kind)
                stats['emitters_decoded'] += 1
                dropped = decoded_dropped(d)
                stats['emitters_decoded_partial'] += 1 if dropped else 0
                for k in dropped:
                    stats['decoded_dropped_' + k] += 1
            emitters.append(r)
            stats['emitters_' + kind] += 1
        if len(emitters) > first:
            hide = float(sysd.get('auto_hide') or 60.0)
            sys_index[path.lower()] = len(systems)
            systems.append((first, len(emitters) - first, min(hide, 150.0)))
    places = []
    x0, y0, x1, y1 = box
    placements = list(placements)
    for f in extra or []:               # (the export's entity effects: the fountains' jets, rings and splashes)
        src = (f.get('props') or {}).get('witcher_particle_system') or ''
        M = np.array(f['matrix'], float)
        # (the fountains' jets carry a squashed scale, x about 0.05: the renderer sizes particles by that axis; the
        # rotation alone)
        R = M[:3, :3] / np.maximum(np.linalg.norm(M[:3, :3], axis=0, keepdims=True), 1e-6)
        placements.append({'path': src, 'pos': M[:3, 3].tolist(), 'rot': R.T.ravel().tolist(), 'emissionScale': 1.0})
    for p in placements:
        s = sys_index.get(p['path'].lower())
        x, y, z = p['pos']
        if s is None or not (x0 <= x <= x1 and y0 <= y <= y1):
            continue
        M = np.zeros((3, 4))
        M[:, :3] = np.array(p['rot'], float).reshape(3, 3).T
        M[:, 3] = p['pos']
        places.append((M, s, float(p.get('emissionScale') or 1.0), 0.0))
        stats['placed'] += 1
    tex_list = sorted(textures, key=textures.get)
    blob = xfx_blob(tex_list, emitters, systems, places, stats, origin)
    out.mkdir(parents=True, exist_ok=True)
    (out / ('%s.xfx' % mapname)).write_bytes(blob)
    (out / ('%s.xfx.json' % mapname)).write_text(json.dumps(
        {'textures': [{'path': k[0], 'file': tex_name(k[0]), 'cols': k[1], 'rows': k[2], 'additive': bool(k[3])}
                      for k in tex_list], 'emitters': emitters, 'systems': systems, 'stats': dict(stats)}, indent=0),
        encoding='utf-8')
    missing, done = [], set()
    with pk3write.writing(out / ('zz_%s_fx.pk3' % mapname), zipfile.ZIP_STORED) as z:
        z.write(out / ('%s.xfx' % mapname), 'maps/%s.xfx' % mapname)
        for k in tex_list:
            name = tex_name(k[0])
            if name in done:
                continue
            img = flipbook(k[0])
            if img is None:
                missing.append(k[0])
                continue
            done.add(name)
            z.writestr(zipfile.ZipInfo(name), cp2gltf.dds_bytes(img, True))
    print('w3fx: %d textures (%d missing%s), %d emitters, %d systems, %d placements (%s) -> %s' % (
        len(tex_list), len(missing), (': ' + ', '.join(missing)) if missing else '', len(emitters), len(systems),
        len(places), ', '.join('%s %d' % kv for kv in sorted(stats.items())), out))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--dump', type=Path, required=True)
    ap.add_argument('--placements', type=Path, required=True)
    ap.add_argument('--out', type=Path, required=True)
    ap.add_argument('--map', default='w3_city')
    ap.add_argument('--decoded', type=Path, help="w3emitter.py's json: the game's own emitter values")
    ap.add_argument('--entity-fx', type=Path, help="the export's placements.json: its entity effects (fx) too")
    ap.add_argument('--build-json', type=Path, help="w3build.py's record (its box and origin)")
    ap.add_argument('--box', help='"x0 y0 x1 y1" (metres, game space): placements kept (default: the build record\'s)')
    ap.add_argument('--origin', help='"x y z": the build\'s origin (default: the build record\'s)')
    ap.add_argument('--w3', help="a research folder whose uncook/ is the add-on's uncook folder (default $W3_UNCOOK, "
                                 "else $W3_RESEARCH/uncook)")
    a = ap.parse_args()
    bj = json.load(open(a.build_json, encoding='utf-8')) if a.build_json else {}
    box = [float(v) for v in a.box.split()] if a.box else bj.get('box')
    origin = [float(v) for v in a.origin.split()] if a.origin else bj.get('origin')
    if box is None or origin is None:
        ap.error('give --build-json, or both --box and --origin (the build\'s frame)')
    global UNCOOK_RESEARCH
    UNCOOK_RESEARCH = a.w3
    build(json.load(open(a.dump, encoding='utf-8')), json.load(open(a.placements, encoding='utf-8')), a.out, a.map,
          [float(v) for v in box], np.array([float(v) for v in origin]),
          json.load(open(a.entity_fx, encoding='utf-8'))['fx'] if a.entity_fx else None,
          json.load(open(a.decoded, encoding='utf-8')) if a.decoded else None)


if __name__ == '__main__':
    main()
