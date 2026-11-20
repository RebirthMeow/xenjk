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
"""CP2077's placed particle effects (steam, smoke, dust, sparks, insects) as a scene's maps/<map>.xfx.
Watson places 30,933 worldStaticParticleNode and 3,912 worldEffectNode; the
scan skipped them all (only effects named "steam" became static haze columns, scene_fogs).

A .particle is a CParticleSystem of CParticleEmitters, each a billboard emitter of modules (initialisers: lifetime, size,
alpha, rotation, spawn sphere / box / circle, position, velocity; modifiers over the particle's life: alpha, size,
rotation rate, velocity, acceleration, drag, flipbook animation, alpha by distance, velocity turbulence, noise, size by
distance) drawn with a material, almost always
base\\fx\\shaders\\blackbodyradiation.mt (181 of 195 emitters fetched: a flipbook FireScatterAlpha of subUV cells,
DarkSmoke / LightSmoke colours, maxAlpha, SoftAlpha). A .effect is a timeline whose effectTrackItemParticles play
particle systems with alpha, size, emission and velocity multipliers (baked into that system's copy of the emitters).
This reads both (as WolvenKit serialises them) into emitter templates, and the sectors' nodes into placements; emitters
named *lodN are the source's own lower levels and are left out.

maps/<map>.xfx, version 1, little endian, every section 16-byte aligned:
  header  char[4] "XFX\\0", u32 version, u32 count[4], u32 offset[4]  (sections: textures, emitters, systems, placements)
  texture 64 B path (the pack's DDS, textures/cp77fx/<hash of the depot path>_<name>.dds; empty: none) + u32 cols,
          u32 rows, u32 flags (bit 0 additive; bit 1 its shape in its value, bit 31 bit 1 decided here), u32 pad  = 80 B
  emitter EMITTER below (f32 unless named; lengths in game units, times in seconds, angles in turns)  = 512 B
  system  u32 first emitter, u32 emitter count, f32 hide distance (game units), f32 pad              = 16 B
  place   f32 m[12] (3x4 rows, game units, the node's scale in it), u32 system, f32 emission scale,
          f32 hide distance (0: the system's), u32 seed                                               = 64 B
Curves are 8 samples over the particle's life (birth: over the emitter's duration, looped), linear between; the samples
are the source curve in its own interpolation (linear, Hermite, quadratic or cubic Bezier). The over-life curves carry
their module's scale. vel_life is a multiplier of the starting velocity, or with F_VEL_LIFE_ABS the speed itself (game
units a second). An emitter with no lifetime initialiser lives its emitter's duration.
    python -I cp77fx.py <work> <fx json root> <out folder> [--map cp_watson]
    python -I cp77fx.py needs <work> <fx fetch> [--out <list>]   the depot paths the fetch still lacks
    python -I cp77fx.py fetch <work> <fx fetch>                  fetch them (wk_fetch.py) until none are new
The fetch's scope is the systems and effects whose file names match --match (steam, smoke, dust, vapour, drips,
liquid nitrogen; steam and smoke matter most) placed on at least --min-nodes visible nodes, or a --roots
list; then the particle systems the effects play and the textures the systems name. The converter reads
<fx fetch>/json and --png <fx fetch>/png."""
import argparse
import collections
import json
import os
import re
import struct
import sys
from concurrent.futures import ProcessPoolExecutor
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
import cp2gltf  # noqa: E402
import wk_fetch  # noqa: E402

N = 8                                           # curve samples
UPM = 37.5
# the emitter record: (name, struct format); the sizes add to 512 B with the pad
EMITTER = [
    ('texture', 'I'), ('flags', 'I'), ('max_particles', 'I'), ('duration', 'f'),
    ('birth', '%df' % N), ('life', '2f'),
    ('size_x', '2f'), ('size_y', '2f'), ('size_life', '%df' % N),
    ('alpha', '2f'), ('alpha_life', '%df' % N),
    ('rot', '2f'), ('rot_rate', '2f'), ('rot_rate_life', '%df' % N),
    ('spawn_radius', '2f'), ('spawn_box', '3f'), ('spawn_force', '2f'),
    ('pos_min', '3f'), ('pos_max', '3f'), ('vel_min', '3f'), ('vel_max', '3f'), ('vel_life', '%df' % N),
    ('accel', '3f'), ('drag', '%df' % N),
    ('anim_fps', 'f'), ('frame', '2f'),
    ('fade_near', '2f'), ('fade_far', '2f'),
    ('dark', '3f'), ('light', '3f'), ('max_alpha', 'f'), ('soft', 'f'),
    ('wind', 'f'), ('lod_distance', 'f'), ('stretch', 'f'),
    # VelocityTurbulize, Noise and SizeByDistance: the record's last 24 floats, 104 on
    ('turb_min', '3f'), ('turb_max', '3f'), ('turb_interval', 'f'), ('turb_life', 'f'),
    ('noise_amp', '3f'), ('noise_freq', 'f'), ('noise_rate', '3f'), ('noise_amp_life', '3f'),
    ('size_near', '3f'), ('size_far', '3f'),
]
EMITTER_FMT = '<' + ''.join(f for _, f in EMITTER)
EMITTER_SIZE = 512
assert struct.calcsize(EMITTER_FMT) <= EMITTER_SIZE, struct.calcsize(EMITTER_FMT)
# flags
F_ADDITIVE, F_UNLIT, F_SPAWN_BOX, F_SPAWN_CIRCLE, F_SPAWN_SPHERE = 1, 2, 4, 8, 16
F_VEL_WORLD, F_VEL_LIFE_ABS, F_STRETCH, F_ORIENTED, F_HEMI_UP = 32, 64, 128, 256, 512
F_HAS_ACCEL, F_FADE_DIST = 1024, 2048
F_TURB, F_NOISE, F_SIZE_DIST = 4096, 8192, 16384
# a texture record's flags beside its additive bit: T_VALUE_MASK, the image's shape is in its value (the renderer takes
# the texel's value as its alpha); T_MASK_DECIDED, set on every texture whose image this build read (a pack made before
# carries neither: the renderer then decides by itself)
T_VALUE_MASK, T_MASK_DECIDED = 2, 0x80000000


def value_mask(img):
    """Whether a flipbook keeps its shape in its value: its alpha solid (99.9% of the texels at 250 or more) on a black
    ground (a quarter or more of the texels under 16 in every channel), the shape drawn in light. In Watson, dust specks (99% black) and motes (52%), vapour spray (48%) and round sparks (53%) drew as solid
    squares, their alpha taken as the mask. Sheets with an alpha of their own (every smoke and steam sheet) and solid
    images without a black ground (noise, white) keep their alpha."""
    a = img[..., 3]
    black = img[..., :3].max(axis=-1) < 16
    return bool((a >= 250).mean() >= 0.999 and black.mean() >= 0.25)


def D(o):
    return (o.get('Data') or {}) if isinstance(o, dict) else {}


def lin(v):
    """CP colour bytes -> linear 0-1"""
    return ((np.array([v.get('Red', 255), v.get('Green', 255), v.get('Blue', 255)], float) / 255.0) ** 2.2).tolist()


def vec(v, axes='XYZ'):
    return np.array([float(v.get(a, 0.0)) for a in axes])


def curve_at(curves, t, f):
    """A curve's value at t in its own interpolation (points beyond its ends hold). `curves` is the evaluator's
    'curves' (InterpolationType, Elements of Point and Value). Linear and Hermite elements are all keys (Hermite with
    monotone tangents from the neighbours: the files store none, and Catmull-Rom's dip steam alphas below 0 after
    their last bright key); BezierCubic elements run key, control, control,
    key, ... and BezierQuadratic key, control, key, ... (every fetched curve has 3k+1 or 2k+1 elements), each a 2D
    (Point, Value) Bezier point."""
    it = curves.get('InterpolationType') or 'Linear'
    els = curves.get('Elements') or []
    pts = [(float(e['Point']), f(e['Value'])) for e in els]
    deg = 3 if it == 'BezierCubic' else 2 if it == 'BezierQuadratic' else 1
    if deg > 1 and (len(pts) - 1) % deg:
        deg = 1                                         # (not that shape: as keys)
    if deg == 1:
        pts.sort(key=lambda p: p[0])                    # (by Point alone: values may be arrays)
    keys = pts[::deg]
    if t <= keys[0][0]:
        return keys[0][1]
    if t >= keys[-1][0]:
        return keys[-1][1]
    i = next(k for k in range(len(keys) - 1) if t <= keys[k + 1][0])
    if deg > 1:
        seg = pts[i * deg:(i + 1) * deg + 1]
        xs = [p[0] for p in seg]
        bern = (lambda u: [(1 - u) ** 2, 2 * u * (1 - u), u * u]) if deg == 2 else \
            (lambda u: [(1 - u) ** 3, 3 * u * (1 - u) ** 2, 3 * u * u * (1 - u), u ** 3])
        a, b = 0.0, 1.0                                 # the parameter whose Point is t (Points rise along a segment)
        for _ in range(40):
            u = (a + b) / 2
            if sum(w * x for w, x in zip(bern(u), xs)) < t:
                a = u
            else:
                b = u
        return sum(w * p[1] for w, p in zip(bern((a + b) / 2), seg))
    (t0, v0), (t1, v1) = keys[i], keys[i + 1]
    h = max(t1 - t0, 1e-6)
    s = (t - t0) / h
    if it != 'Hermite':
        return v0 + (v1 - v0) * s

    def secant(k):
        return (np.asarray(keys[k + 1][1], float) - keys[k][1]) / max(keys[k + 1][0] - keys[k][0], 1e-6)

    def tangent(k):                                     # (Fritsch-Butland: 0 at a turn, so no overshoot of the keys)
        if k == 0 or k == len(keys) - 1:
            return secant(min(k, len(keys) - 2))
        d0, d1 = secant(k - 1), secant(k)
        h0, h1 = max(keys[k][0] - keys[k - 1][0], 1e-6), max(keys[k + 1][0] - keys[k][0], 1e-6)
        same = d0 * d1 > 0
        q0, q1 = np.where(same, d0, 1.0), np.where(same, d1, 1.0)
        return np.where(same, 3 * (h0 + h1) / ((2 * h1 + h0) / q0 + (h1 + 2 * h0) / q1), 0.0)
    return ((2 * s ** 3 - 3 * s * s + 1) * v0 + (s ** 3 - 2 * s * s + s) * h * tangent(i) +
            (-2 * s ** 3 + 3 * s * s) * v1 + (s ** 3 - s * s) * h * tangent(i + 1))


def curve_samples(d, f, n):
    """A curve evaluator's values at n points over [0, 1], or None when it has no elements"""
    c = d.get('curves') or {}
    if not c.get('Elements'):
        return None
    vs = [curve_at(c, x, f) for x in np.linspace(0.0, 1.0, n)]
    return [float(v) for v in vs] if np.ndim(vs[0]) == 0 else vs


def ev_range(e, f=float):
    """An evaluator as the range an initialiser draws from: (lo, hi) (a curve: the least and most it reaches over
    [0, 1])"""
    d = D(e)
    t = d.get('$type', '')
    if t.endswith('Const'):
        v = f(d.get('value', 0.0))
        return v, v
    if t.endswith('RandomUniform') or t.endswith('Random'):
        return f(d.get('min', 0.0)), f(d.get('max', 0.0))
    if t.endswith('StartEnd'):
        return f(d.get('start', 0.0)), f(d.get('end', 0.0))
    if t.endswith('Curve'):
        vs = curve_samples(d, f, 33) or [f(0.0)]
        return (min(vs), max(vs)) if not isinstance(vs[0], np.ndarray) else (np.min(vs, 0), np.max(vs, 0))
    return f(0.0), f(0.0)


def ev_mean(e, f=float):
    """An evaluator as one value: a constant's, a range's middle, a curve's mean over [0, 1]"""
    d = D(e)
    if d.get('$type', '').endswith('Curve'):
        vs = curve_samples(d, f, 33)
        if vs is not None:
            return np.mean(vs, 0) if isinstance(vs[0], np.ndarray) else float(np.mean(vs))
    lo, hi = ev_range(e, f)
    return (lo + hi) / 2


def multi_curve(d, n):
    """A CEvaluatorVectorMultiCurve's xyz at n points over [0, 1], linear between its keys, or None. Its Data (base64):
    per channel u32 count, u32 offset of its points, u32 offset of its values (bytes from the start), then the floats"""
    import base64
    c = d.get('curves') or {}
    try:
        b = base64.b64decode(c.get('Data', ''))
        out = []
        for ch in range(min(int(c.get('NumChannels', 0)), 3)):
            k, po, vo = struct.unpack_from('<3I', b, ch * 12)
            pts, vals = struct.unpack_from('<%df' % k, b, po), struct.unpack_from('<%df' % k, b, vo)
            out.append(np.interp(np.linspace(0.0, 1.0, n), pts, vals))
    except (ValueError, struct.error):
        return None
    if not out:
        return None
    while len(out) < 3:
        out.append(out[-1])
    return np.stack(out, 1)


def ev_curve(e, f=float, default=1.0):
    """An evaluator as N samples over [0, 1]"""
    d = D(e)
    t = d.get('$type', '')
    ts = np.linspace(0.0, 1.0, N)
    if not t:
        return [default] * N
    if t.endswith('Curve'):
        vs = curve_samples(d, f, N)
        return [float(v) for v in vs] if vs is not None else [default] * N
    lo, hi = ev_range(e, f)
    if t.endswith('StartEnd'):
        return [float(lo + (hi - lo) * x) for x in ts]
    return [float((lo + hi) / 2)] * N


def fvec(v):
    return vec(v, 'XYZ') if isinstance(v, dict) else np.full(3, float(v))


def num(v, default=1.0):
    """A module's number: a plain value, or an evaluator's middle"""
    if isinstance(v, dict):
        lo, hi = ev_range(v)
        return (lo + hi) / 2
    return float(v if v is not None else default)


def fx(v):
    return float(v.get('X', 0.0)) if isinstance(v, dict) else float(v)


def material(e, textures):
    """The emitter's texture (an index into textures), its flags and colours"""
    mi = D(e.get('localMaterialInstance'))
    base = cp2gltf.depot(mi.get('baseMaterial')).split('\\')[-1].lower()
    vals = {}
    for v in mi.get('values', []):
        for k, x in v.items():
            if k != '$type':
                vals[k] = x
    flags = 0
    tex, cols, rows = '', 1, 1
    dark = light = [1.0, 1.0, 1.0]
    max_alpha, soft = 1.0, 1.0
    if base == 'blackbodyradiation.mt':
        tex = cp2gltf.depot(vals.get('FireScatterAlpha'))
        cols, rows = int(vals.get('subUVWidth', 1) or 1), int(vals.get('subUVHeight', 1) or 1)
        dark = lin(vals['DarkSmoke']) if 'DarkSmoke' in vals else [0.77, 0.77, 0.77]
        light = lin(vals['LightSmoke']) if 'LightSmoke' in vals else [1.0, 1.0, 1.0]
        max_alpha, soft = float(vals.get('maxAlpha', 1.0)), float(vals.get('SoftAlpha', 1.0))
        flags |= F_UNLIT if vals.get('DoNotApplyLighting') else 0
    elif base == 'particles_generic.remt':
        tex = cp2gltf.depot(vals.get('Diffuse'))
        c = vals.get('Color')
        dark = light = (np.array(lin(c)) * float(vals.get('ColorMultiplier', 1.0))).tolist() if c else light
        max_alpha, soft = float(vals.get('AlphaGlobal', 1.0)), float(vals.get('AlphaSoft', 1.0))
        flags |= F_UNLIT
    elif base == 'transparent_liquid.mt':
        tex = cp2gltf.depot(vals.get('Albedo')) or cp2gltf.depot(vals.get('Normal'))
        cols, rows = int(vals.get('SubUVWidth', 1) or 1), int(vals.get('SubUVHeight', 1) or 1)
        c = vals.get('ScatteringColorThin')
        dark = light = lin(c) if c else light
    flags |= F_ADDITIVE if float(vals.get('AdditiveAlphaBlend', 0) or 0) > 0.5 else 0
    key = (tex.lower(), cols, rows, flags & F_ADDITIVE)
    if key not in textures:
        textures[key] = len(textures)
    return textures[key], flags, dark, light, max_alpha, soft


def emitter(e, textures):
    """One emitter as the EMITTER record's fields (CP metres; to game units at write), or None when it is off"""
    if not e.get('isEnabled', 1):
        return None
    mods = {}
    for m in e.get('modules', []):
        d = D(m)
        if d.get('isEnabled', 1):
            mods[d['$type'][len('CParticle'):]] = d
    lod = (e.get('lods') or [{}])[0]
    tex, flags, dark, light, max_alpha, soft = material(e, textures)
    r = {'texture': tex, 'max_particles': int(e.get('maxParticles', 0) or 0)}
    r['duration'] = float((lod.get('emitterDurationSettings') or {}).get('emitterDuration', 1.0) or 1.0)
    r['birth'] = ev_curve(lod.get('birthRate'), default=0.0)
    r['lod_distance'] = float(lod.get('lodSwitchDistance', 0.0) or 0.0)
    g = lambda k: mods.get(k, {})
    # (no lifetime initialiser: the emitter's duration, so its particles live, rather than (0, 0) and none drawn)
    r['life'] = ev_range(g('InitializerLifeTime').get('lifeTime')) if 'InitializerLifeTime' in mods else \
        (r['duration'], r['duration'])
    sz = g('InitializerSize')
    lo, hi = ev_range(sz.get('size'), fvec)
    s = num(sz.get('scale'))
    r['size_x'] = (float(lo[0]) * s, float(hi[0]) * s)
    one = D(sz.get('size')).get('freeAxes') == 'FVA_One'
    r['size_y'] = r['size_x'] if one else (float(lo[1]) * s, float(hi[1]) * s)
    sl = g('ModificatorSizeOverLife')
    r['size_life'] = [x * num(sl.get('scale')) for x in ev_curve(sl.get('size'), fx)] if sl else [1.0] * N
    r['alpha'] = ev_range(g('InitializerAlpha').get('alpha')) if 'InitializerAlpha' in mods else (1.0, 1.0)
    al = g('ModificatorAlphaOverLife')
    r['alpha_life'] = ev_curve(al.get('alpha')) if al else [1.0] * N
    r['rot'] = ev_range(g('InitializerRotation').get('rotation'))
    r['rot_rate'] = ev_range(g('InitializerRotationRate').get('rotationRate'))
    rl = g('ModificatorRotationRateOverLife')
    r['rot_rate_life'] = ev_curve(rl.get('rotationRate')) if rl else [1.0] * N
    sp = g('InitializerSpawnSphere') or g('InitializerSpawnCircle')
    r['spawn_radius'] = (ev_range(sp.get('innerRadius'))[1], ev_range(sp.get('outerRadius'))[1]) if sp else (0.0, 0.0)
    r['spawn_force'] = ev_range(sp.get('forceScale')) if sp and sp.get('velocity') else (0.0, 0.0)
    if 'InitializerSpawnSphere' in mods:
        flags |= F_SPAWN_SPHERE | (F_HEMI_UP if not sp.get('spawnNegativeZ', 1) else 0)
    elif 'InitializerSpawnCircle' in mods:
        flags |= F_SPAWN_CIRCLE
    bx = g('InitializerSpawnBox')
    r['spawn_box'] = tuple(fvec(D(bx.get('extents')).get('value', {})).tolist()) if bx else (0.0, 0.0, 0.0)
    flags |= F_SPAWN_BOX if bx else 0
    po = g('InitializerPosition')
    # ('offset' is a switch, 0 or 1; the evaluator is 'position')
    lo, hi = ev_range(po.get('position'), fvec) if po else (np.zeros(3), np.zeros(3))
    r['pos_min'], r['pos_max'] = tuple(lo.tolist()), tuple(hi.tolist())
    ve = g('InitializerVelocity')
    lo, hi = ev_range(ve.get('velocity'), fvec) if ve else (np.zeros(3), np.zeros(3))
    vs = num(ve.get('scale')) if ve else 1.0
    r['vel_min'], r['vel_max'] = tuple((lo * vs).tolist()), tuple((hi * vs).tolist())
    flags |= F_VEL_WORLD if ve.get('worldSpace') else 0
    vl = g('ModificatorVelocityOverLife')
    # (absolute: the curve is the speed, CP metres a second, made game units at write; else a multiplier)
    r['vel_life'] = [x * num(vl.get('scale')) for x in ev_curve(vl.get('velocity'), fx)] if vl else [1.0] * N
    flags |= F_VEL_LIFE_ABS if vl and vl.get('absolute') else 0
    ac = g('ModificatorAcceleration')
    # (a direction curve over the life: its mean; the record holds one acceleration)
    r['accel'] = tuple((np.asarray(ev_mean(ac.get('direction'), fvec), float) * num(ac.get('scale'))).tolist()) \
        if ac else (0.0, 0.0, 0.0)
    flags |= F_HAS_ACCEL if ac else 0
    dr = g('ModificatorDrag')
    r['drag'] = [x * num(dr.get('scale')) for x in ev_curve(dr.get('dragCoefficient'), default=0.0)] if dr else [0.0] * N
    an = g('ModificatorTextureAnimation')
    r['anim_fps'] = ev_range(an.get('animationSpeed'))[1] if an else 0.0
    r['frame'] = ev_range(an.get('initialFrame')) if an else (0.0, 0.0)
    fd = g('ModificatorAlphaByDistance')
    r['fade_near'] = (fx(fd.get('nearBlendDistance', {})), float((fd.get('nearBlendDistance') or {}).get('Y', 0))) if fd else (0.0, 0.0)
    r['fade_far'] = (fx(fd.get('farBlendDistance', {})), float((fd.get('farBlendDistance') or {}).get('Y', 0))) if fd else (0.0, 0.0)
    flags |= F_FADE_DIST if fd else 0
    r['dark'], r['light'], r['max_alpha'], r['soft'] = dark, light, max_alpha, soft
    r['wind'] = float(e.get('windInfluence', 0.0) or 0.0)
    # velocity turbulence: random kicks every noiseInterval s within timelifeLimit of the life (a curve: its mean), world
    # axes; a single-axis range is the same on all three
    tb = g('ModificatorVelocityTurbulize')
    lo, hi = ev_range(tb.get('scale'), fvec) if tb else (np.zeros(3), np.zeros(3))
    if tb and D(tb.get('scale')).get('freeAxes') == 'FVA_One':
        lo, hi = np.full(3, float(lo[0])), np.full(3, float(hi[0]))
    r['turb_min'], r['turb_max'] = tuple(np.asarray(lo, float).tolist()), tuple(np.asarray(hi, float).tolist())
    r['turb_interval'] = float(tb.get('noiseInterval', 0.0) or 0.0) if tb else 0.0
    r['turb_life'] = float(np.clip(ev_mean(tb.get('timelifeLimit')), 0.0, 1.0)) if tb else 0.0
    flags |= F_TURB if tb and r['turb_interval'] > 0.0 and np.any(np.abs(hi - lo) + np.abs(hi) > 0) else 0
    # noise on the velocity (applyToPosition is 0 in every fetched system): amplitude (m/s; a curve over the life: its
    # largest, and the curve's share at 0, 1/2 and 1), frequency (per metre; one axis), change rate (per second)
    nz = g('ModificatorNoise')
    one = lambda k: D(nz.get(k)).get('freeAxes') == 'FVA_One'
    if nz:
        ad = D(nz.get('amplitude'))
        if ad.get('$type', '').endswith('Curve'):
            vs = multi_curve(ad, 33) if ad['$type'].endswith('MultiCurve') else curve_samples(ad, fvec, 33)
            vs = np.asarray(vs if vs is not None else [np.zeros(3)], float)
            amp = vs.max(0)
            peak = max(float(np.abs(vs).max()), 1e-6)
            life = [float(np.abs(vs[i]).max()) / peak for i in (0, 16, 32)]
        else:
            amp, life = np.asarray(ev_mean(nz.get('amplitude'), fvec), float), [1.0, 1.0, 1.0]
        if one('amplitude'):
            amp = np.full(3, float(amp[0]))
        fq = np.asarray(ev_mean(nz.get('frequency'), fvec), float)
        rate = nz.get('changeRate') or {}
        r['noise_amp'], r['noise_freq'] = tuple(amp.tolist()), float(fq[0] if one('frequency') else fq.mean())
        r['noise_rate'] = (fx(rate.get('X', 0.0)), float(rate.get('Y', 0.0)), float(rate.get('Z', 0.0)))
        r['noise_amp_life'] = tuple(life)
        flags |= F_NOISE if np.any(amp != 0) else 0
    else:
        r['noise_amp'], r['noise_freq'], r['noise_rate'], r['noise_amp_life'] = (0.0,) * 3, 0.0, (0.0,) * 3, (0.0,) * 3
    # size by the eye's distance (metres): near multiplier at the near range's start to 1 at its end, 1 at the far
    # range's start to the far multiplier at its end
    sd = g('ModificatorSizeByDistance')
    m = lambda k: float(ev_mean(sd.get(k)))
    r['size_near'] = (m('nearDistanceRangeStart'), m('nearDistanceRangeEnd'), m('nearDistanceSizeMultiplier')) if sd else (0.0, 0.0, 1.0)
    r['size_far'] = (m('farDistanceRangeStart'), m('farDistanceRangeEnd'), m('farDistanceSizeMultiplier')) if sd else (0.0, 0.0, 1.0)
    flags |= F_SIZE_DIST if sd else 0
    drawer = D(e.get('particleDrawer')).get('$type', '')
    r['stretch'] = 1.0 if drawer == 'CParticleDrawerMotionBlur' else 0.0
    flags |= F_STRETCH if drawer == 'CParticleDrawerMotionBlur' else 0
    flags |= F_ORIENTED if drawer == 'CParticleDrawerEmitterOrientation' else 0
    r['flags'] = flags
    r['name'] = e.get('editorName', '')
    return r


def serialised(root, path):
    """A resource's serialised json: WolvenKit's convert serialize writes them flat, by file name"""
    for f in (root / (path.lower().replace('\\', '/') + '.json'), root / (path.lower().split('\\')[-1] + '.json')):
        if f.exists():
            return f
    return None


def particle_system(path, root, textures):
    """A .particle's emitters (the main levels) and its hide distance, or None when it was not fetched"""
    f = serialised(root, path)
    if f is None:
        return None
    rc = json.load(open(f, encoding='utf-8'))['Data']['RootChunk']
    ems = []
    for e in rc.get('emitters', []):
        e = D(e)
        if re.search(r'lod\d', e.get('editorName', ''), re.I):
            continue
        r = emitter(e, textures)
        if r is not None:
            ems.append(r)
    return {'emitters': ems, 'hide': float(rc.get('autoHideDistance', 0.0) or 0.0)}


def effect_items(path, root):
    """An .effect's particle tracks: [(particle path, alpha, size, emission, velocity)] (the multipliers at its start;
    each is an effectEffectParameterEvaluatorFloat, its evaluator a handle)"""
    f = serialised(root, path)
    if f is None:
        return None
    out = []

    def walk(o):
        if isinstance(o, dict):
            if o.get('$type') == 'effectTrackItemParticles':
                k = lambda n: ev_curve(o[n].get('evaluator'))[0] if isinstance(o.get(n), dict) else 1.0
                out.append((cp2gltf.depot(o.get('particleSystem')), k('alpha'), k('size'), k('emissionScale'),
                            k('velocity')))
            for v in o.values():
                walk(v)
        elif isinstance(o, list):
            for v in o:
                walk(v)
    walk(json.load(open(f, encoding='utf-8'))['Data']['RootChunk'])
    return out


def placements(work, centre=None, radius=None):
    """The sectors' particle and effect nodes: [(kind, path, 4x4 CP matrix, emission rate, hide distance)]"""
    out = []
    for f in sorted((work / 'json').glob('*.streamingsector.json')):
        rc = json.load(open(f, encoding='utf-8'))['Data']['RootChunk']
        nodes = rc['nodes']
        for r in cp2gltf.shown_records(work, f.name.split('.')[0], rc):     # variant-off ranges stay out
            n = nodes[r['NodeIndex']]['Data']
            t = n.get('$type')
            if t not in ('worldStaticParticleNode', 'worldEffectNode') or not n.get('isVisibleInGame', 1):
                continue
            p = r['Position']
            if centre is not None and not cp2gltf.in_area(float(p.get('X', 0)), float(p.get('Y', 0)), centre, radius,
                                                          float(p.get('Z', 0))):
                continue
            M = cp2gltf.trs(p, r['Orientation'], r.get('Scale'))
            if t == 'worldStaticParticleNode':
                out.append(('particle', cp2gltf.depot(n.get('particleSystem')), M, float(n.get('emissionRate', 1.0) or 1.0),
                            float(n.get('forcedAutoHideDistance', 0.0) or 0.0)))
            else:
                out.append(('effect', cp2gltf.depot(n.get('effect')), M, 1.0, float(n.get('streamingDistanceOverride', 0.0) or 0.0)))
    return out


def build(work, root, out, mapname, origin, centre=None, radius=None, png=None):
    textures, systems, emitters = {}, {}, []
    sys_rows = []                       # (first emitter, count, hide)
    stats = collections.Counter()

    def system_index(path, alpha=1.0, size=1.0, emission=1.0, velocity=1.0):
        key = (path.lower(), round(alpha, 3), round(size, 3), round(emission, 3), round(velocity, 3))
        if key in systems:
            return systems[key]
        ps = particle_system(path, root, textures)
        if ps is None or not ps['emitters']:
            systems[key] = None
            return None
        first = len(emitters)
        for e in ps['emitters']:
            e = dict(e)
            e['alpha'] = tuple(a * alpha for a in e['alpha'])
            e['size_x'] = tuple(a * size for a in e['size_x'])
            e['size_y'] = tuple(a * size for a in e['size_y'])
            e['birth'] = [b * emission for b in e['birth']]
            # (the track's velocity: the starting velocities, and an absolute speed curve)
            e['vel_min'] = tuple(a * velocity for a in e['vel_min'])
            e['vel_max'] = tuple(a * velocity for a in e['vel_max'])
            e['spawn_force'] = tuple(a * velocity for a in e['spawn_force'])
            if e['flags'] & F_VEL_LIFE_ABS:
                e['vel_life'] = [a * velocity for a in e['vel_life']]
            emitters.append(e)
        sys_rows.append((first, len(ps['emitters']), ps['hide']))
        systems[key] = len(sys_rows) - 1
        return systems[key]

    places = []
    for kind, path, M, rate, hide in placements(work, centre, radius):
        if kind == 'particle':
            items = [(path, 1.0, 1.0, 1.0, 1.0)]
        else:
            items = effect_items(path, root)
            if items is None:
                stats['effect_not_fetched'] += 1
                continue
        for p, al, sz, em, vel in items:
            s = system_index(p, al, sz, em, vel)
            if s is None:
                stats['system_not_fetched'] += 1
                continue
            places.append((M, s, rate, hide))
            stats['placed_' + kind] += 1
    # each texture's mask (value_mask), decided from the image pack() will encode; none without the png folder
    masks = {}
    if png is not None:
        from PIL import Image
        for k in textures:
            src = png_of(Path(png), k[0]) if k[0] else None
            if src is not None:
                masks[k[0]] = value_mask(np.asarray(Image.open(src).convert('RGBA')))
        stats['textures_value_mask'] = sum(masks.values())
        stats['textures_mask_decided'] = len(masks)
    blob = xfx_blob(textures, emitters, sys_rows, places, stats, origin, masks)
    out.mkdir(parents=True, exist_ok=True)
    (out / ('%s.xfx' % mapname)).write_bytes(blob)
    meta = {'textures': [{'path': k[0], 'file': tex_name(k[0]), 'cols': k[1], 'rows': k[2], 'additive': bool(k[3]),
                          'value_mask': masks.get(k[0])} for k in textures],
            'emitters': [{k: v for k, v in e.items()} for e in emitters], 'systems': sys_rows,
            'stats': dict(stats)}
    (out / ('%s.xfx.json' % mapname)).write_text(json.dumps(meta, indent=0), encoding='utf-8')
    print('cp77fx: %d textures, %d emitters, %d systems, %d placements (%s) -> %s' % (
        len(textures), len(emitters), len(sys_rows), len(places),
        ', '.join('%s %d' % kv for kv in sorted(stats.items())), out))
    return textures


def tex_name(path):
    """A flipbook's image in the pack: textures/cp77fx/<cp2gltf.cache_name of its depot path>.dds (a hash of the
    whole path, so one file name in two folders stays two images), its name cut to fit the record's 64 B with a NUL
    (the hash keeps it unique); '' for none"""
    if not path:
        return ''
    name = cp2gltf.cache_name(path.lower()).rsplit('.', 1)[0]
    return 'textures/cp77fx/%s.dds' % name[:63 - len('textures/cp77fx/.dds')]


def xfx_blob(textures, emitters, sys_rows, places, stats, origin=None, masks=None):
    """The .xfx (header in the module's docstring). Lengths to game units: game = (CP - origin) * UPM; a placement's
    3x3 stays unitless (it carries the node's scale), as the emitters' lengths are game units already."""
    pad = lambda b: b.ljust((len(b) + 15) & ~15, b'\0')
    masks = masks or {}
    tf = lambda k: k[3] | ((T_MASK_DECIDED | (T_VALUE_MASK if masks[k[0]] else 0)) if k[0] in masks else 0)
    tex = b''.join(tex_name(k[0]).encode('ascii').ljust(64, b'\0') + struct.pack('<4I', k[1], k[2], tf(k), 0)
                   for k in textures)
    assert len(tex) == 80 * len(textures)
    L = UPM
    ems = []
    for e in emitters:
        v = dict(e)
        for k in ('size_x', 'size_y', 'spawn_radius', 'spawn_box', 'pos_min', 'pos_max', 'vel_min', 'vel_max', 'accel',
                  'fade_near', 'fade_far'):
            v[k] = tuple(float(x) * L for x in v[k])
        v['lod_distance'] *= L
        v['spawn_force'] = tuple(float(x) * L for x in v['spawn_force'])
        v['turb_min'] = tuple(float(x) * L for x in v['turb_min'])
        v['turb_max'] = tuple(float(x) * L for x in v['turb_max'])
        v['noise_amp'] = tuple(float(x) * L for x in v['noise_amp'])
        v['noise_freq'] = float(v['noise_freq']) / L
        v['size_near'] = (v['size_near'][0] * L, v['size_near'][1] * L, v['size_near'][2])
        v['size_far'] = (v['size_far'][0] * L, v['size_far'][1] * L, v['size_far'][2])
        if v['flags'] & F_VEL_LIFE_ABS:                 # (an absolute speed curve, not a multiplier)
            v['vel_life'] = [float(x) * L for x in v['vel_life']]
        vals = []
        for name, f in EMITTER:
            x = v[name]
            vals += list(x) if isinstance(x, (list, tuple)) else [x]
        ems.append(struct.pack(EMITTER_FMT, *vals).ljust(EMITTER_SIZE, b'\0'))
    sysb = b''.join(struct.pack('<2I2f', f, c, h * L, 0.0) for f, c, h in sys_rows)
    pl = []
    for k, (M, s, rate, hide) in enumerate(places):
        W = np.array(M[:3, :4], float)
        W[:, 3] = (W[:, 3] - origin) * L
        pl.append(struct.pack('<12fIffI', *W.ravel(), s, rate, hide * L, k * 2654435761 & 0xffffffff))
    body = [tex, b''.join(ems), sysb, b''.join(pl)]
    counts = [len(textures), len(ems), len(sys_rows), len(pl)]
    offs, at = [], 48
    for b in body:
        offs.append(at)
        at += len(pad(b))
    return struct.pack('<4sI4I4I', b'XFX\0', 1, *counts, *offs).ljust(48, b'\0') + b''.join(pad(b) for b in body)


def pack(textures, png, out, mapname):
    """zz_<map>_fx.pk3: the .xfx and each flipbook as tex_name(its depot path) (BC7, the uncooked png: <png>/<depot
    path>.png as WolvenKit uncooks it, else the one png of that file name); a texture with no png is reported and
    left out (the renderer draws its emitters untextured)."""
    import zipfile
    from PIL import Image
    by_stem = collections.defaultdict(list)
    for p in png.rglob('*.png'):
        by_stem[p.stem.lower()].append(p)
    missing, done = [], set()
    dst = out / ('zz_%s_fx.pk3' % mapname)
    tmp = dst.with_name(dst.name + '.tmp')      # (renamed when whole: a stopped write leaves no pack; no zip64, which
    with zipfile.ZipFile(tmp, 'w', zipfile.ZIP_STORED, allowZip64=False) as z:     # the engine cannot open)
        z.write(out / ('%s.xfx' % mapname), 'maps/%s.xfx' % mapname)
        for k in textures:
            name = tex_name(k[0])
            # (one image may serve as additive and as blended: one file)
            if not name or name in done:
                missing += [] if name else ['(none)']
                continue
            rel = Path(k[0].replace('\\', '/'))
            src = png / rel.with_suffix('.png')
            if not src.exists():
                same = by_stem.get(rel.stem.lower(), [])
                src = same[0] if len(same) == 1 else None
            if src is None:
                missing.append(k[0])
                continue
            done.add(name)
            img = np.asarray(Image.open(src).convert('RGBA'))
            z.writestr(zipfile.ZipInfo(name), cp2gltf.dds_bytes(img, True))
    os.replace(tmp, dst)
    print('cp77fx: %s, %d textures%s' % (out / ('zz_%s_fx.pk3' % mapname), len(textures) - len(missing),
                                         ('; no png for ' + ', '.join(missing)) if missing else ''))


# ---------------------------------------------------------------------------------------------------- fetch

# the scope: the steam, smoke and dust systems and effects (with drips and liquid nitrogen) placed on 15 or more
# visible nodes. Watson: 40 effects exactly, and 98 systems (a cut by hand of a census of
# every record) with 34 more of 15-28 nodes; --roots takes a list instead
MATCH = r'smoke|steam|dust|vapou?r|drip|nitrogen'
MIN_NODES = 15


def _sector_resources(args):
    work, f = args
    out = collections.Counter()
    data = open(f, 'rb').read()
    if b'worldStaticParticleNode' not in data and b'worldEffectNode' not in data:
        return out
    rc = json.loads(data)['Data']['RootChunk']
    nodes = rc['nodes']
    for r in cp2gltf.shown_records(work, Path(f).name.split('.')[0], rc):
        n = nodes[r['NodeIndex']]['Data']
        t = n.get('$type')
        if t == 'worldStaticParticleNode' and n.get('isVisibleInGame', 1):
            out[wk_fetch.norm(cp2gltf.depot(n.get('particleSystem')))] += 1
        elif t == 'worldEffectNode' and n.get('isVisibleInGame', 1):
            out[wk_fetch.norm(cp2gltf.depot(n.get('effect')))] += 1
    return out


def _init_worker(work, variants):
    cp2gltf._SHOWN_VARIANTS[str(work)] = variants


def node_resources(work, workers):
    """{particle system or effect depot path: visible nodes placing it} over the work folder's sectors (the records
    placements() reads)"""
    files = sorted((Path(work) / 'json').glob('*.streamingsector.json'))
    variants = cp2gltf.sector_variants(Path(work)) or {}
    total = collections.Counter()
    with ProcessPoolExecutor(workers, initializer=_init_worker, initargs=(str(work), variants)) as ex:
        for c in ex.map(_sector_resources, [(str(work), str(f)) for f in files], chunksize=16):
            total.update(c)
    total.pop('', None)
    return total


def fetch_roots(work, workers, match=MATCH, min_nodes=MIN_NODES):
    """The placed systems and effects in the fetch's scope"""
    rx = re.compile(match, re.I)
    return sorted(p for p, c in node_resources(work, workers).items()
                  if c >= min_nodes and p.endswith(('.particle', '.effect')) and rx.search(p.split('\\')[-1]))


def png_of(png, path):
    """A texture's uncooked png as pack() finds it: <png>/<depot path>.png, else the one png of that file name"""
    rel = Path(path.replace('\\', '/'))
    f = png / rel.with_suffix('.png')
    if f.exists():
        return f
    same = list(png.rglob(rel.stem + '.png'))
    return same[0] if len(same) == 1 else None


def needs(roots, root, retry=False):
    """What the fx fetch at root still lacks: the systems and effects in scope, the systems the effects play, the
    textures the systems name (looked up as build() and pack() look them up). Returns {'need', 'absent' (needed but
    in fetch_absent.txt; retry: none are), 'counts'}"""
    root = Path(root)
    absent = set() if retry else wk_fetch.Fetched(root).absent
    seen, todo, need = set(), list(roots), set()
    counts = collections.Counter()
    while todo:
        p = todo.pop()
        if p in seen:
            continue
        seen.add(p)
        counts[p.rsplit('.', 1)[-1]] += 1
        if p.endswith('.xbm'):
            if png_of(root / 'png', p) is None:
                need.add(p)
            continue
        f = serialised(root / 'json', p)
        if f is None:
            need.add(p)
            continue
        refs = ('.particle',) if p.endswith('.effect') else ('.particle', '.xbm')
        todo += sorted(wk_fetch.walk_refs(json.load(open(f, encoding='utf-8'))['Data']['RootChunk'], refs, set()) - seen)
    return {'need': sorted(need - absent), 'absent': sorted(need & absent), 'counts': dict(counts)}


def fetch_loop(roots, root, retry=False, rounds=8):
    """Fetch until nothing new is needed (retry: the paths in fetch_absent.txt are asked for again, once)"""
    tried = set()
    for k in range(rounds):
        n = needs(roots, root, retry and k == 0)
        new = [p for p in n['need'] if p not in tried]
        print('cp77fx fetch: round %d: %d needed (%d new), %d absent' % (k + 1, len(n['need']), len(new),
                                                                         len(n['absent'])), flush=True)
        if not new:
            return n
        wk_fetch.fetch(new, root)
        tried |= set(new)
    n = needs(roots, root)
    print('cp77fx fetch: stopped after %d rounds, %d still needed' % (rounds, len(n['need'])), flush=True)
    return n


def fetch_main(argv):
    ap = argparse.ArgumentParser(prog='cp77fx.py ' + argv[0],
                                 description='needs: write the depot paths the fx fetch at <root> still lacks; '
                                             'fetch: fetch them (wk_fetch.py) until nothing new is needed')
    ap.add_argument('work', type=Path, help='the area work folder (json/*.streamingsector.json)')
    ap.add_argument('root', type=Path, help='the fx fetch folder (wk_fetch.py layout: json/, png/)')
    ap.add_argument('--match', default=MATCH, help='the systems and effects in scope, by file name (default %(default)s)')
    ap.add_argument('--min-nodes', type=int, default=MIN_NODES, help='placed on at least this many visible nodes')
    ap.add_argument('--roots', type=Path, help='the systems and effects in scope as a list (one depot path a line), '
                                               'instead of --match and --min-nodes')
    ap.add_argument('--out', type=Path, help='needs: the list to write (default <root>/needs.txt)')
    ap.add_argument('--retry', action='store_true', help='ask again for the paths in <root>/fetch_absent.txt')
    ap.add_argument('--workers', type=int, default=max(2, min(16, (os.cpu_count() or 4) - 2)))
    a = ap.parse_args(argv[1:])
    if a.roots:
        roots = sorted({wk_fetch.norm(l) for l in a.roots.read_text(encoding='utf-8').splitlines() if l.strip()})
    else:
        roots = fetch_roots(a.work, a.workers, a.match, a.min_nodes)
    print('cp77fx %s: %d systems and effects in scope' % (argv[0], len(roots)), flush=True)
    if argv[0] == 'fetch':
        n = fetch_loop(roots, a.root, a.retry)
    else:
        n = needs(roots, a.root, a.retry)
        out = a.out or a.root / 'needs.txt'
        wk_fetch.write_list(n['need'], out)
        print('cp77fx needs: walked %s; %d needed -> %s; %d absent (fetch_absent.txt)' % (
            ', '.join('%s %d' % kv for kv in sorted(n['counts'].items())), len(n['need']), out, len(n['absent'])))
    return 1 if n['need'] else 0


def main():
    if len(sys.argv) > 1 and sys.argv[1] in ('needs', 'fetch'):
        return fetch_main(sys.argv[1:])
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('work', type=Path, help='the area work folder (json/*.streamingsector.json)')
    ap.add_argument('root', type=Path, help='the serialised fx (WolvenKit convert serialize output)')
    ap.add_argument('out', type=Path)
    ap.add_argument('--map', default='cp_watson')
    ap.add_argument('--origin', default='-1280 2240 12', help="the scene's origin in CP metres (Watson's hub frame)")
    ap.add_argument('--png', type=Path, help='the uncooked textures (WolvenKit uncook --uext png): also write the pack')
    a = ap.parse_args()
    textures = build(a.work, a.root, a.out, a.map, np.array([float(v) for v in a.origin.split()]), png=a.png)
    if a.png:
        pack(textures, a.png, a.out, a.map)


if __name__ == '__main__':
    sys.exit(main())
