"""The Witcher 3's COOKED particle emitters decoded, so a port can use the game's own particle values
(w3fx.py's KINDS are hand-set because the Blender add-on's reader expects uncooked module objects).

A cooked .w2p's CParticleEmitter has no `modules` array. Cooking folds every module into (1) two bit masks,
initializerSetMask and modifierSetMask (INIT_BITS / MOD_BITS below; numInitializers / numModifiers are their bit
counts), (2) `seeds`, one {key: that initializer's mask bit, val: seed} per initializer, and (3) a binary tail
after the chunk's properties: one fixed record, every field always present, in the FIELDS order below, then 16 zero
bytes. The field order follows WolvenKit 7's SParticleEmitterModuleData (WolvenKit is open source, GPL-3.0; its Read
walks the same field list; no code is taken from it); the mask bits, the 16-byte pad and what the samples mean are from all 8,800 emitters of the game's 3,797
.w2p (every tail parses to exactly 16 zero bytes from its end).

Tail field kinds: a sample buffer is a count byte (0x80: empty, the "negative zero" of the engine's variable-length
int; else the count, 6 bits + continuation) and count x 1, 2 or 3 little-endian floats; the rest are plain f32, u32,
u64, bool bytes, a 3-float vector and a 16-float matrix. So the example's "80 80 02 9a99d93f 00002040" is: alpha (none),
colour (none), life time 2 samples 1.7 and 2.5.

What the samples are (the cook bakes each module's evaluator):
  - 2 samples: a constant (both equal), a start/end pair, or with the module's random bit set the minimum and maximum
    of a uniform random pick;
  - 16 samples: a curve baked at t = i/15, linear between;
  - an initializer without the random bit is read over the EMITTER's normalised time, a modifier over the PARTICLE's
    normalised life;
  - 4, 18 or 32 samples (0.4% of buffers): two modules of one type cooked one after the other (2+2, 2+16, 16+16). The
    first block is decoded; every block is under 'stacked' (which of them the game plays is not known).
Lengths are metres and velocities metres a second in the emitter's space (Witcher 3: z up), colours 0-1 (authored
bytes / 255), times seconds; rotations are as stored (turns, by their 0..1 ranges; rates a second).

decode_emitter(tail, init_mask, mod_mask) -> the dict described at its definition; decode_w2p(bytes) -> a system's
emitters with their plain properties (name, max particles, drawer, material, birth rate) beside the decoded modules.
    python tools/levelport/w3emitter.py [--list FILE] [depot path or file ...] [-o OUT.json] [--raw]
        [--game DIR] [--index CACHE.pkl]
(depot paths are read from the game's bundles by w3bundle.py; --index caches the bundle index, ~290,000 names)"""
import argparse
import json
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

N = 8                                           # samples a curve is given back in (cp77fx's N)
PAD = 16                                        # zero bytes after the record

# initializerSetMask: bit -> (module, the random variant)
INIT_BITS = {
    0: ('alpha', False), 1: ('alpha', True), 2: ('color', False), 3: ('color', True),
    4: ('life', False), 5: ('life', True), 6: ('position', False), 7: ('position', True),
    8: ('rotation', False), 9: ('rotation', True), 10: ('rotation3d', False), 11: ('rotation3d', True),
    12: ('rotation_rate', False), 13: ('rotation_rate', True),
    14: ('rotation_rate3d', False), 15: ('rotation_rate3d', True),
    16: ('size', False), 17: ('size', True), 18: ('size3d', False), 19: ('size3d', True),
    20: ('spawn_box', False), 21: ('spawn_circle', False), 22: ('spawn_sphere', False),
    23: ('velocity', False), 24: ('velocity', True),
    25: ('velocity_inherit', False), 26: ('velocity_inherit', True), 27: ('velocity_spread', False),
    28: ('frame', False), 29: ('frame', True), 30: ('collision_spawn', False),
}
# modifierSetMask: bit -> (module, variant). Two bits of one module are its two modes; which is which is from the data
# where the data can tell, and None where it cannot:
#   velocity_life  bit 1's curves are equal on x, y, z (multipliers), bit 0's one emitter is a speed along x;
#   size_life      every bit 13 emitter has a size initializer to multiply (bit 12 is never set);
#   color_life     85% of bit 6's emitters have a colour initializer (grey ramps to multiply), 24% of bit 7's;
#   texture_animation  bit 20's speeds are frame rates (30, 12, 20, 16), bit 21's frame counts (63, 32, 64, 48);
#   acceleration   bit 3 is the common one (98%; 86% of them straight down): taken as world space, bit 2 as local;
#   alpha_life     bits 4 and 5 could not be told apart (modulate or absolute; the same thing on the 84% of emitters
#                  with no alpha initializer): the mode is None, the bit is in 'mod_bits'.
MOD_BITS = {
    0: ('velocity_life', 'absolute'), 1: ('velocity_life', 'modulate'),
    2: ('acceleration', 'local'), 3: ('acceleration', 'world'),
    4: ('alpha_life', None), 5: ('alpha_life', None),
    6: ('color_life', 'modulate'), 7: ('color_life', 'absolute'),
    9: ('rotation_life', None), 11: ('rotation_rate_life', None), 13: ('size_life', 'modulate'),
    15: ('rotation3d_life', None), 16: ('rotation_rate3d_life', None), 19: ('size3d_life', None),
    20: ('texture_animation', 'speed'), 21: ('texture_animation', 'life'),
    22: ('target', None), 23: ('target_node', None), 24: ('turbulence', None),
    25: ('alpha_over_effect', None),        # (no data in the record; named by elimination: the one module left)
    26: ('collision', None), 27: ('alpha_by_distance', None),
}

S1, S2, S3, F32, U32, U64, BOOL, VEC3, MAT = 's1', 's2', 's3', 'f', 'I', 'Q', 'b', 'v3', 'm'
# the record: (name, kind). Names are this module's; WolvenKit's where they differ are in the comments.
FIELDS = [
    ('alpha', S1), ('color', S3), ('life', S1), ('position', S3), ('position_offset', F32),
    ('rotation', S1), ('rotation3d', S3), ('rotation_rate', S1), ('rotation_rate3d', S3),
    ('size', S2), ('size3d', S3), ('size_keep_ratio', BOOL),
    ('spawn_extents', S3),
    ('spawn_outer_radius', S1),             # (WolvenKit: spawnInnerRadius; the larger of the two in 98% of emitters)
    ('spawn_inner_radius', S1),             # (WolvenKit: spawnOuterRadius)
    ('spawn_world_space', BOOL), ('spawn_surface_only', BOOL),
    ('p0A8', VEC3),                         # (always 0)
    ('spawn_to_local', MAT),                # (identity on a spawn circle, else 0)
    ('velocity', S3), ('velocity_world_space', BOOL),
    ('velocity_inherit', S1), ('velocity_spread', S1), ('velocity_spread_conserve_momentum', BOOL),
    ('frame', S1),                          # (texture animation's initial frame)
    ('p140', U32), ('p144', U32), ('p148', U32),    # (always 0)
    ('velocity_life', S3), ('acceleration_direction', S3), ('acceleration_scale', S1),
    ('rotation_life', S1), ('rotation_rate_life', S1), ('rotation3d_life', S3), ('rotation_rate3d_life', S3),
    ('color_life', S3), ('alpha_life', S1), ('size_life', S2),
    ('size3d_life', S3),                    # (WolvenKit: sizeOverLifeOrientation)
    ('animation_speed', S1),
    ('turbulence_scale', S3), ('turbulence_life_limit', S1), ('turbulence_noise_interval', F32),
    ('turbulence_duration', F32),
    ('force_scale', S1),                    # (WolvenKit: targetForceScale; a spawn shape's push too)
    ('target_kill_radius', S1), ('target_max_force', F32), ('target_position', S3),
    ('spawn_positive_x', BOOL), ('spawn_negative_x', BOOL), ('spawn_positive_y', BOOL), ('spawn_negative_y', BOOL),
    ('spawn_positive_z', BOOL), ('spawn_negative_z', BOOL), ('spawn_velocity', BOOL),
    ('collision_group_mask', U64), ('collision_dynamic_friction', F32), ('collision_static_friction', F32),
    ('collision_restitution', F32), ('collision_velocity_dampening', F32), ('collision_disable_gravity', BOOL),
    ('collision_use_gpu', BOOL), ('collision_radius', F32), ('collision_kill_when_collide', BOOL),
    ('collision_self_emitter_index', U32), ('collision_spawn_probability', F32),
    ('collision_spawn_parent_emitter_index', U32),
    ('alpha_by_distance_far', F32), ('alpha_by_distance_near', F32),
]


class TailError(ValueError):
    pass


def _vlq(d, p):
    b = d[p]
    p += 1
    n = b & 0x3F
    if b & 0x40:
        sh = 6
        while True:
            c = d[p]
            p += 1
            n |= (c & 0x7F) << sh
            sh += 7
            if not c & 0x80:
                break
    return n, p


def read_record(tail):
    """The tail's fields as stored: {name: value}, a sample buffer a list of floats or float tuples; and the offset
    the record ends at (len(tail) - 16 in every file of the game)"""
    d, p, out = tail, 0, {}
    try:
        for name, kind in FIELDS:
            if kind in (S1, S2, S3):
                n, p = _vlq(d, p)
                k = int(kind[1])
                if p + 4 * k * n > len(d):
                    raise TailError('%s: %d samples pass the tail\'s end' % (name, n))
                fl = struct.unpack_from('<%df' % (k * n), d, p)
                p += 4 * k * n
                out[name] = list(fl) if k == 1 else [fl[i * k:(i + 1) * k] for i in range(n)]
            elif kind == BOOL:
                out[name] = bool(d[p])
                p += 1
            elif kind == VEC3:
                out[name] = struct.unpack_from('<3f', d, p)
                p += 12
            elif kind == MAT:
                out[name] = struct.unpack_from('<16f', d, p)
                p += 64
            else:
                out[name] = struct.unpack_from('<' + kind, d, p)[0]
                p += struct.calcsize(kind)
    except (struct.error, IndexError) as e:
        raise TailError('the tail ends inside the record (%s)' % e) from None
    if p > len(d):
        raise TailError('the record passes the tail\'s end')
    return out, p


def _blocks(s):
    """A buffer's modules: [samples] of 2 or 16 each (None: a count that is neither of one nor of two modules)"""
    n = len(s)
    if n in (0, 2, 16):
        return [s] if n else []
    if n == 4:
        return [s[:2], s[2:]]
    if n == 32:
        return [s[:16], s[16:]]
    if n == 18:                             # (2+16 or 16+2: a constant's two samples are equal)
        return [s[:2], s[2:]] if s[0] == s[1] else [s[:16], s[16:]]
    return None


def resample(s, n=N):
    """Evenly spaced samples (linear between) as n evenly spaced ones"""
    if not s:
        return None
    m = len(s)
    out = []
    for j in range(n):
        x = j * (m - 1) / (n - 1) if m > 1 else 0.0
        i = min(int(x), max(m - 2, 0))
        f = x - i
        a, b = s[i], s[min(i + 1, m - 1)]
        if isinstance(a, (tuple, list)):
            out.append([a[c] + (b[c] - a[c]) * f for c in range(len(a))])
        else:
            out.append(a + (b - a) * f)
    return out


def _l(v):
    return list(v) if isinstance(v, tuple) else v


def decode_emitter(tail, init_mask=0, mod_mask=0, raw=False):
    """One cooked emitter's modules from its tail bytes (the bytes after the CParticleEmitter chunk's properties) and
    its two masks. Anything the emitter does not have, or that could not be decoded, is None.

    Initializers (each value is [a, b]: with `<name>_random` the uniform pick's two ends, else the value at the
    emitter's start and end; `<name>_emitter` is the 8-sample curve over the emitter's time when it is not constant):
      life (s), alpha, color ([r, g, b] 0-1), rotation, rotation_rate, rotation3d, rotation_rate3d,
      size ([x, y] m; size_keep_ratio: one pick for both axes), size3d, position ([x, y, z] m) + position_offset (m),
      velocity ([x, y, z] m/s; velocity_world), velocity_inherit, velocity_spread, frame (texture animation's first)
      spawn: None or {shape 'box' | 'circle' | 'sphere', extents [a, b] (box: half sizes), outer_radius [a, b],
             inner_radius [a, b], world_space, surface_only, to_local (circle: 16 floats), sides {+x -x +y -y +z -z}
             (sphere), velocity (the shape pushes its particles outward), force [a, b] (that push)}
    Modifiers over the particle's life, 8 samples each (`<name>_mode`: 'modulate' multiplies the start value,
    'absolute' replaces it, None: not known, see MOD_BITS; alpha_life's is always None):
      alpha_life, color_life ([r, g, b]), size_life ([x, y]), size3d_life, velocity_life ([x, y, z]),
      rotation_life, rotation_rate_life, rotation3d_life, rotation_rate3d_life
      accel ([x, y, z] m/s2 at birth = direction x scale), accel_life (8 x [x, y, z] when it changes), accel_world
            (inferred, see MOD_BITS)
      anim: None or {mode 'speed' | 'life', speed [a, b] (speed mode: frames a second; life mode: frames passed
            over the whole life, 0 = the first frame stays, e.g. 25 on a 5x5 sheet with a 10 s life), speed_life
            (8 samples when baked as a curve)}; the first frame is `frame`
      turbulence, target, collision, alpha_by_distance: None or their fields as stored; alpha_over_effect: bool
    Also: initializers, modifiers (the masks' module names), init_bits, mod_bits (the set bits), unknown_bits (set
    bits this table has no name for), stacked ({name: every block} where two modules of a type were cooked together;
    the value is the first), undecoded (buffers whose sample count fits no module: None), tail_left (bytes after the
    record: 16) and tail_ok (they are 16 zeros)."""
    rec, end = read_record(tail)
    left = tail[end:]
    o = {'init_mask': init_mask, 'mod_mask': mod_mask, 'tail_left': len(left),
         'tail_ok': len(left) == PAD and not any(left)}
    inits, mods, unknown = {}, {}, []
    for b in range(32):
        if init_mask >> b & 1:
            if b in INIT_BITS:
                name, rnd = INIT_BITS[b]
                inits[name] = inits.get(name, False) or rnd
            else:
                unknown.append('init %d' % b)
        if mod_mask >> b & 1:
            if b in MOD_BITS:
                mods.setdefault(MOD_BITS[b][0], MOD_BITS[b][1])
            else:
                unknown.append('mod %d' % b)
    o['initializers'], o['modifiers'], o['unknown_bits'] = sorted(inits), sorted(mods), unknown
    o['init_bits'] = [b for b in range(32) if init_mask >> b & 1]
    o['mod_bits'] = [b for b in range(32) if mod_mask >> b & 1]
    stacked, undecoded = {}, []

    def first(name):
        bl = _blocks(rec[name])
        if bl is None:
            undecoded.append(name)
            return None
        if len(bl) > 1:
            stacked[name] = [[_l(x) for x in b] for b in bl]
        return bl[0] if bl else None

    for name, field in (('life', 'life'), ('alpha', 'alpha'), ('color', 'color'), ('rotation', 'rotation'),
                        ('rotation_rate', 'rotation_rate'), ('rotation3d', 'rotation3d'),
                        ('rotation_rate3d', 'rotation_rate3d'), ('size', 'size'), ('size3d', 'size3d'),
                        ('position', 'position'), ('velocity', 'velocity'), ('velocity_inherit', 'velocity_inherit'),
                        ('velocity_spread', 'velocity_spread'), ('frame', 'frame')):
        s = first(field) if name in inits else None
        o[name] = [_l(s[0]), _l(s[-1])] if s else None
        o[name + '_random'] = bool(inits.get(name)) if s else None
        o[name + '_emitter'] = resample(s) if s and not inits.get(name) and any(x != s[0] for x in s) else None
    o['size_keep_ratio'] = rec['size_keep_ratio'] if o['size'] else None
    o['position_offset'] = rec['position_offset'] if 'position' in inits else None
    o['velocity_world'] = rec['velocity_world_space'] if o['velocity'] else None
    o['velocity_spread_conserve_momentum'] = rec['velocity_spread_conserve_momentum'] if o['velocity_spread'] else None

    shape = [n[6:] for n in ('spawn_box', 'spawn_circle', 'spawn_sphere') if n in inits]
    if shape:
        def pair(f):
            s = first(f)
            return [_l(s[0]), _l(s[-1])] if s else None
        sp = {'shape': shape[-1], 'shapes': shape, 'world_space': rec['spawn_world_space'],
              'surface_only': rec['spawn_surface_only'], 'velocity': rec['spawn_velocity'],
              'force': pair('force_scale') if rec['spawn_velocity'] else None,
              'extents': pair('spawn_extents') if 'box' in shape else None,
              'outer_radius': pair('spawn_outer_radius') if shape[-1] != 'box' else None,
              'inner_radius': pair('spawn_inner_radius') if shape[-1] != 'box' else None,
              'to_local': list(rec['spawn_to_local']) if 'circle' in shape else None,
              'sides': {k: rec['spawn_' + n] for k, n in (('+x', 'positive_x'), ('-x', 'negative_x'),
                                                           ('+y', 'positive_y'), ('-y', 'negative_y'),
                                                           ('+z', 'positive_z'), ('-z', 'negative_z'))}
              if 'sphere' in shape else None}
        o['spawn'] = sp
    else:
        o['spawn'] = None

    for name in ('alpha_life', 'color_life', 'size_life', 'size3d_life', 'velocity_life', 'rotation_life',
                 'rotation_rate_life', 'rotation3d_life', 'rotation_rate3d_life'):
        s = first(name) if name in mods else None
        o[name] = resample(s) if s else None
        o[name + '_mode'] = mods.get(name) if s else None
    if 'acceleration' in mods:
        dr, sc = first('acceleration_direction'), first('acceleration_scale')
        if dr and sc:
            dr, sc = resample(dr), resample(sc)
            al = [[c * k for c in v] for v, k in zip(dr, sc)]
            o['accel'] = al[0]
            o['accel_life'] = al if any(v != al[0] for v in al) else None
            o['accel_world'] = mods['acceleration'] == 'world'
        else:
            o['accel'] = o['accel_life'] = o['accel_world'] = None
    else:
        o['accel'] = o['accel_life'] = o['accel_world'] = None
    if 'texture_animation' in mods:
        s = first('animation_speed')
        o['anim'] = {'mode': mods['texture_animation'], 'speed': [s[0], s[-1]] if s else None,
                     'speed_life': resample(s) if s and len(s) > 2 else None}
    else:
        o['anim'] = None
    if 'turbulence' in mods:
        sc, lim = first('turbulence_scale'), first('turbulence_life_limit')
        o['turbulence'] = {'scale': [_l(sc[0]), _l(sc[-1])] if sc else None,
                           'life_limit': [lim[0], lim[-1]] if lim else None,
                           'noise_interval': rec['turbulence_noise_interval'], 'duration': rec['turbulence_duration']}
    else:
        o['turbulence'] = None
    if 'target' in mods or 'target_node' in mods:
        fs, kr, tp = first('force_scale'), first('target_kill_radius'), first('target_position')
        o['target'] = {'node': 'target_node' in mods, 'force_scale': [fs[0], fs[-1]] if fs else None,
                       'kill_radius': [kr[0], kr[-1]] if kr else None, 'max_force': rec['target_max_force'],
                       'position': [_l(tp[0]), _l(tp[-1])] if tp else None}
    else:
        o['target'] = None
    if 'collision' in mods or 'collision_spawn' in inits:
        o['collision'] = {k[10:]: rec[k] for k in rec if k.startswith('collision_')}
        o['collision']['modifier'] = 'collision' in mods
        o['collision']['spawn'] = 'collision_spawn' in inits
    else:
        o['collision'] = None
    o['alpha_by_distance'] = ({'near': rec['alpha_by_distance_near'], 'far': rec['alpha_by_distance_far']}
                              if 'alpha_by_distance' in mods else None)
    o['alpha_over_effect'] = 'alpha_over_effect' in mods
    o['stacked'] = stacked
    o['undecoded'] = undecoded              # (buffers whose sample count fits no module: their value is None)
    if raw:
        o['raw'] = {k: [_l(x) for x in v] if isinstance(v, list) else _l(v) for k, v in rec.items()}
    return o


# ---- the file around the tail: a small CR2W property reader (from the data lane's cr2w_dump.py) --------------------

class CR2W:
    def __init__(self, d):
        if d[:4] != b'CR2W':
            raise ValueError('not a CR2W file')
        self.d = d
        tabs = [struct.unpack_from('<3I', d, 40 + 12 * i) for i in range(10)]
        so = tabs[0][0]
        self.names = []
        for i in range(tabs[1][1]):
            o = struct.unpack_from('<I', d, tabs[1][0] + 8 * i)[0]
            self.names.append(d[so + o:d.index(b'\0', so + o)].decode('latin-1'))
        self.imports = []
        for i in range(tabs[2][1]):
            o = struct.unpack_from('<I', d, tabs[2][0] + 8 * i)[0]
            self.imports.append(d[so + o:d.index(b'\0', so + o)].decode('latin-1'))
        self.chunks = []                    # (class, parent chunk + 1, data offset, data size)
        for i in range(tabs[4][1]):
            cn, _fl, par, dsz, doff, _tm, _crc = struct.unpack_from('<HHIIIII', d, tabs[4][0] + 24 * i)
            self.chunks.append((self.names[cn], par, doff, dsz))

    def ref(self, v):
        """A pointer: ('chunk', index), ('import', path) or None"""
        if v > 0:
            return ('chunk', v - 1)
        if v < 0 and -v - 1 < len(self.imports):
            return ('import', self.imports[-v - 1])
        return None

    def value(self, t, p, end):
        d = self.d
        simple = {'Float': '<f', 'Int32': '<i', 'Uint32': '<I', 'Uint16': '<H', 'Int16': '<h', 'Uint8': '<B',
                  'Int8': '<b', 'Uint64': '<Q', 'Bool': '<?'}
        if t in simple:
            return struct.unpack_from(simple[t], d, p)[0], p + struct.calcsize(simple[t])
        if t == 'CName':
            return self.names[struct.unpack_from('<H', d, p)[0]], p + 2
        if t == 'String':
            b = d[p]
            n, q = b & 0x3F, p + 1
            if b & 0x40:
                sh = 6
                while True:
                    c = d[q]
                    q += 1
                    n |= (c & 0x7F) << sh
                    sh += 7
                    if not c & 0x80:
                        break
            if b & 0x80:
                return d[q:q + n].decode('latin-1'), q + n
            return d[q:q + 2 * n].decode('utf-16-le', 'replace'), q + 2 * n
        if t.startswith(('handle:', 'ptr:')):
            return self.ref(struct.unpack_from('<i', d, p)[0]), p + 4
        if t.startswith('array:'):
            inner = t.split(',', 2)[2]
            n = struct.unpack_from('<I', d, p)[0]
            p += 4
            out = []
            for _ in range(n):
                if p >= end:
                    break
                v, p = self.value(inner, p, end)
                out.append(v)
            return out, p
        if t.startswith('E') and not t.startswith('Emitter') and t != 'EulerAngles':    # an enum: its value's name
            v = struct.unpack_from('<H', d, p)[0]
            return (self.names[v] if v < len(self.names) else v), end
        props, q = self.props(p + 1 if d[p] == 0 else p, end)                           # a struct
        return props, q

    def props(self, p, end):
        d, out = self.d, {}
        while p + 2 <= end:
            ni = struct.unpack_from('<H', d, p)[0]
            if ni == 0:
                return out, p + 2
            ti, size = struct.unpack_from('<HI', d, p + 2)
            pend = p + 4 + size
            try:
                out[self.names[ni]] = self.value(self.names[ti], p + 8, pend)[0]
            except Exception:  # noqa: BLE001  (a type this reader does not know: kept as bytes)
                out[self.names[ni]] = d[p + 8:pend]
            p = pend
        return out, p

    def chunk(self, i):
        """A chunk's (class, properties, the bytes after them)"""
        cn, _par, off, sz = self.chunks[i]
        pr, q = self.props(off + 1, off + sz)
        return cn, pr, self.d[q:off + sz]


def _evaluator(f, ref):
    """A float evaluator chunk as [a, b] (None: a curve, whose baked samples this reader does not decode)"""
    if not ref or ref[0] != 'chunk':
        return None, None
    cn, pr, _tail = f.chunk(ref[1])
    if cn == 'CEvaluatorFloatConst':
        v = float(pr.get('value', 0.0))
        return cn, [v, v]
    if cn == 'CEvaluatorFloatRandomUniform':
        return cn, [float(pr.get('min', 0.0)), float(pr.get('max', 0.0))]
    if cn == 'CEvaluatorFloatStartEnd':
        return cn, [float(pr.get('start', 0.0)), float(pr.get('end', 0.0))]
    return cn, None


def _material(f, ref):
    if not ref:
        return None
    if ref[0] == 'import':
        return {'base': ref[1], 'params': {}}
    cn, pr, tail = f.chunk(ref[1])
    base = pr.get('baseMaterial')
    out = {'base': base[1] if base and base[0] == 'import' else None, 'params': {}}
    if cn == 'CMaterialInstance' and len(tail) >= 4:        # the instance's parameters: u32 count, then sized rows
        d, p = tail, 4
        for _ in range(struct.unpack_from('<I', d, 0)[0]):
            if p + 8 > len(d):
                break
            size, ni, ti = struct.unpack_from('<IHH', d, p)
            try:
                v = None
                t = f.names[ti]
                if t == 'Float':
                    v = struct.unpack_from('<f', d, p + 8)[0]
                elif t.startswith('handle:'):
                    r = f.ref(struct.unpack_from('<i', d, p + 8)[0])
                    v = r[1] if r and r[0] == 'import' else None
                elif t in ('Color', 'Vector'):
                    v = d[p + 8:p + size].hex()
                out['params'][f.names[ni]] = v
            except Exception:  # noqa: BLE001
                pass
            p += size
    return out


def decode_w2p(data, raw=False):
    """A cooked .w2p: {'emitters': [decode_emitter's dict + name, max_particles, loops, drawer, material
    {base, params}, birth_rate [a, b] per LOD (None: a curve), keep_local, wind_influence, seeds], ...},
    'auto_hide': the system's autoHideDistance}. An UNCOOKED emitter (a `modules` array, no tail) comes back with
    'cooked': False and no module fields."""
    f = CR2W(data)
    out = {'emitters': [], 'auto_hide': None}
    for i, (cn, _par, _off, _sz) in enumerate(f.chunks):
        if cn == 'CParticleSystem':
            pr = f.chunk(i)[1]
            out['auto_hide'] = pr.get('autoHideDistance')
            out['auto_hide_range'] = pr.get('autoHideRange')
        if cn != 'CParticleEmitter':
            continue
        _cn, pr, tail = f.chunk(i)
        if 'modules' in pr or not tail:
            e = {'cooked': False}
        else:
            e = decode_emitter(tail, int(pr.get('initializerSetMask', 0)), int(pr.get('modifierSetMask', 0)), raw=raw)
            e['cooked'] = True
        drawer = pr.get('particleDrawer')
        lods = []
        for lod in pr.get('lods') or []:
            if not isinstance(lod, dict):
                continue
            kind, rate = _evaluator(f, lod.get('birthRate'))
            lods.append({'birth_rate': rate, 'birth_rate_evaluator': kind,
                         'bursts': lod.get('burstList') if isinstance(lod.get('burstList'), list) else None,
                         'enabled': lod.get('isEnabled', True)})
        dprops = {}
        if drawer and drawer[0] == 'chunk':
            dprops = {k: v for k, v in f.chunk(drawer[1])[1].items() if isinstance(v, (int, float, str, bool))}
        e.update({
            'chunk': i, 'name': pr.get('editorName'), 'max_particles': pr.get('maxParticles'),
            'loops': pr.get('emitterLoops'), 'keep_local': pr.get('keepSimulationLocal', False),
            'wind_influence': pr.get('windInfluence'), 'env_color_group': pr.get('envColorGroup'),
            'drawer': f.chunks[drawer[1]][0] if drawer and drawer[0] == 'chunk' else None, 'drawer_props': dprops,
            'material': _material(f, pr.get('material')), 'lods': lods,
            'birth_rate': lods[0]['birth_rate'] if lods else None,
            'seeds': [[s.get('key', 0), s.get('val', 0)] for s in pr.get('seeds') or [] if isinstance(s, dict)],
        })
        out['emitters'].append(e)
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('paths', nargs='*', help='depot paths (fx\\water\\...\\x.w2p) or files on disk')
    ap.add_argument('--list', help='a text file of depot paths, one a line')
    ap.add_argument('-o', '--out', help='the JSON to write (default: standard output)')
    ap.add_argument('--raw', action='store_true', help='add each emitter\'s record as stored')
    ap.add_argument('--game', help='the game\'s folder (default: $W3_GAME, else $W3_ROOT)')
    ap.add_argument('--index', help='a bundle index cache (pickle; written when missing)')
    a = ap.parse_args()
    paths = list(a.paths)
    if a.list:
        paths += [ln.strip() for ln in Path(a.list).read_text(encoding='utf-8').splitlines() if ln.strip()]
    idx, out, bad = None, {}, 0
    for p in paths:
        if Path(p).is_file():
            data = Path(p).read_bytes()
        else:
            if idx is None:
                import w3bundle
                idx = w3bundle.index(a.game, cache=a.index)
            import w3bundle
            data = w3bundle.read(idx, p)
        if data is None:
            out[p] = {'error': 'not in the bundles'}
            bad += 1
            continue
        try:
            out[p] = decode_w2p(data, raw=a.raw)
        except Exception as e:  # noqa: BLE001
            out[p] = {'error': '%s: %s' % (type(e).__name__, e)}
            bad += 1
    text = json.dumps(out, indent=1)
    if a.out:
        Path(a.out).write_text(text, encoding='utf-8')
        n = sum(len(v.get('emitters') or []) for v in out.values())
        ok = sum(1 for v in out.values() for e in v.get('emitters') or [] if e.get('tail_ok'))
        print('%d systems, %d emitters, %d tails read to their end, %d errors -> %s' % (len(out), n, ok, bad, a.out))
    else:
        sys.stdout.write(text + '\n')
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
