"""w3fx_dump.py -- Witcher 3 particle systems (.w2p) into a json of emitters for w3fx.py (Beauclair).

Runs inside Blender with the Witcher 3 add-on (its particle reader and module settings, as its particle preview reads
them; one Blender at a time, the shared uncook files race):
    blender -b --python tools/levelport/w3fx_dump.py -- --list LIST --out OUT.json
LIST: one .w2p depot path a line. Per system its emitters: drawer, lifetime, birth rate and bursts, the most particles,
spawn shape, position, velocity range, acceleration, base size range, the size, alpha and velocity curves over the
life (8 samples), rotation and its rate, the flipbook (atlas, speed, first frame), the material, its texture (the
uncooked .dds's absolute path, fetched when missing) and the material's parameters."""
import importlib
import json
import os
import sys
import traceback

PKG = 'bl_ext.user_default.witcher3_tools'
argv = sys.argv[sys.argv.index('--') + 1:]
LIST = argv[argv.index('--list') + 1]
OUT = argv[argv.index('--out') + 1]
N = 8

ip = importlib.import_module(PKG + '.importers.import_particle')
dcp = importlib.import_module(PKG + '.CR2W.dc_particle')
mat = importlib.import_module(PKG + '.repo_paths.materialize')


def plain(v, depth=0):
    if depth > 4:
        return None
    if isinstance(v, (int, float, str, bool)) or v is None:
        return v
    if isinstance(v, (list, tuple)):
        return [plain(x, depth + 1) for x in v]
    if isinstance(v, dict):
        return {str(k): plain(x, depth + 1) for k, x in v.items()}
    for a in ('value', 'Value'):
        if hasattr(v, a):
            return plain(getattr(v, a), depth + 1)
    try:
        return [float(x) for x in v]
    except Exception:  # noqa: BLE001
        return str(v)[:120]


def samples(ev, default):
    if ev is None:
        return None
    out = []
    for k in range(N):
        t = k / (N - 1)
        try:
            v = ip._evaluate(ev, t, default)
        except Exception:  # noqa: BLE001
            return None
        v = plain(v)
        out.append(v)
    return out


def scalar_range(ev, default):
    if ev is None:
        return [default, default]
    lo = ip._get(ev, 'minimum', ip._get(ev, 'min', None))
    hi = ip._get(ev, 'maximum', ip._get(ev, 'max', None))
    if lo is None and hi is None:
        v = ip._scalar(ev, 0.0, default)
        return [float(v), float(v)]
    lo = hi if lo is None else lo
    hi = lo if hi is None else hi
    try:
        return [float(plain(lo) if not isinstance(plain(lo), list) else plain(lo)[0]),
                float(plain(hi) if not isinstance(plain(hi), list) else plain(hi)[0])]
    except Exception:  # noqa: BLE001
        return [default, default]


def dds_of(tex):
    if not tex:
        return ''
    t = tex.replace('/', '\\')
    for cand in (t[:-4] + '.dds' if t.lower().endswith('.xbm') else t, t):
        try:
            p = mat.materialize_repo_path(cand, source_game='w3')
            if p and os.path.isfile(str(p)) and str(p).lower().endswith('.dds'):
                return str(p)
        except Exception:  # noqa: BLE001
            pass
    try:
        p = mat.materialize_repo_path(t, source_game='w3')
        if p:
            d = os.path.splitext(str(p))[0] + '.dds'
            if os.path.isfile(d):
                return d
    except Exception:  # noqa: BLE001
        pass
    return ''


res = {}
for line in open(LIST, encoding='utf-8'):
    path = line.strip()
    if not path:
        continue
    try:
        src = mat.materialize_repo_path(path.replace('/', '\\'), source_game='w3')
        system = dcp.load_bin_particle(str(src))
        ems = []
        for em in (ip._get(system, 'emitters', ()) or ()):
            s = ip._settings(em)
            life_ev = ip._evaluator(ip._module(em, 'CParticleInitializerLifeTime'), 'lifeTime', 'life_time')
            ems.append({
                'name': s.name, 'drawer': s.drawer, 'lifetime': s.lifetime, 'life_range': scalar_range(life_ev, s.lifetime),
                'birth_rate': s.birth_rate, 'burst_count': s.burst_count, 'burst_interval': s.burst_interval,
                'loops': s.emitter_loops, 'max_particles': s.max_particles, 'lod_enabled': s.lod_enabled,
                'spawn_radius': s.spawn_radius, 'spawn_position': list(s.spawn_position),
                'spawn_box': list(s.spawn_box_extents), 'sphere_inner': s.spawn_sphere_inner,
                'sphere_outer': s.spawn_sphere_outer, 'sphere_surface': s.spawn_sphere_surface,
                'sphere_velocity': s.spawn_sphere_velocity, 'sphere_force': s.spawn_sphere_force,
                'vel_min': list(s.velocity_min), 'vel_max': list(s.velocity_max), 'accel': list(s.acceleration),
                'vel_life': samples(s.velocity_evaluator, (1.0, 1.0, 1.0, 1.0)), 'vel_modulate': s.velocity_modulate,
                'size_min': list(s.base_size_min), 'size_max': list(s.base_size_max),
                'size_life': samples(s.size_evaluator, (1.0, 1.0, 1.0, 1.0)), 'size_modulate': s.size_modulate,
                'alpha_life': samples(s.alpha_evaluator, 1.0),
                'rot': scalar_range(s.initial_rotation, 0.0), 'rot_rate': scalar_range(s.rotation_rate, 0.0),
                'anim_fps': s.animation_speed, 'anim_mode': s.animation_mode,
                'frame': scalar_range(s.initial_frame, 0.0), 'atlas': [s.atlas_width, s.atlas_height],
                'material': s.material_path, 'texture': s.texture_path, 'texture_dds': dds_of(s.texture_path),
                'params': plain(s.material_parameters),
            })
        res[path] = {'emitters': ems, 'auto_hide': plain(ip._get(system, 'autoHideDistance', ip._get(system, 'auto_hide_distance', None)))}
    except Exception as e:  # noqa: BLE001
        res[path] = {'error': repr(e), 'trace': traceback.format_exc()[-600:]}
with open(OUT, 'w', encoding='utf-8') as f:
    json.dump(res, f, indent=1)
print('W3FX_DUMP done', len(res), sum(1 for v in res.values() if 'error' in v), 'errors')
